// SPDX-License-Identifier: GPL-2.0-or-later

/* System headers */
#include <unistd.h>
#include <poll.h>
#include <netinet/ip.h>
#include <sys/timerfd.h>

/* GLib2 headers */
#include <glib.h>

/* AV headers */
#include <av_sip.h>
#include <av_thread.h>
#include <av_threadcomm.h>
#include <av_config.h>
#include <av_audio.h>

enum CALL_DIRECTION {
	SIP_CALL_OUTGOING,
	SIP_CALL_INCOMING
};

struct av_sip_state {
	struct eXosip_t *sipctx;
	struct av_thread *self;
	struct pollfd poll_data[4];
	struct itimerspec automatic_action_timer;
	int reg_id;
	struct av_modem_config *sipconf;
	eXosip_event_t *current_call_event;
	struct av_thread *audiothread;
	struct av_rtp_connection *current_call_connection;
	gchar *current_call_path;
	int local_rtp_port;
} *sstate;

static struct av_rtp_connection *av_sip_rtp_connection_alloc(const char *addr, int rtp_port, const gchar *serial_device) {
	struct av_rtp_connection *c;

	c = g_try_malloc0(sizeof *c);
	if (c) {
		c->addr = g_strdup(addr);
		c->port = rtp_port;

		if (serial_device)
			c->serial_device = g_strdup(serial_device);
	}

	return c;
}

static void av_sip_rtp_connection_free(struct av_rtp_connection **c) {
	if (*c) {
		g_clear_pointer(&(*c)->addr, g_free);
		g_clear_pointer(&(*c)->serial_device, g_free);
		g_clear_pointer(c, g_free);
	}
}

static gint av_sip_stackteardown(void) {
	if (close(sstate->poll_data[1].fd))
		g_printerr("Failure closing SIP events socket: %s\n",strerror(errno));

	sstate->poll_data[1].fd = -1;

	eXosip_quit(sstate->sipctx);
	osip_free(sstate->sipctx);
	sstate->sipctx = NULL;

	av_config_free(&sstate->sipconf);

	return 0;
}

static gint av_sip_stacksetup(void) {
	if ( !(sstate->sipctx = eXosip_malloc()) ) {
		g_printerr("Failure allocating SIP context\n");
		return 1;
	}

	if (eXosip_init(sstate->sipctx)) {
		g_printerr("Failure initializzing eXosip library\n");
		goto failure;
	}

#ifdef AV_SIP_DEBUG
	osip_trace_initialize(6, NULL);
#endif

	sstate->poll_data[1].fd = eXosip_event_geteventsocket(sstate->sipctx);
	if (sstate->poll_data[1].fd < 0) {
		g_printerr("Unable to get SIP event socket\n");
		goto failure;
	}
	sstate->poll_data[1].events = POLLIN;

	if (eXosip_listen_addr(sstate->sipctx, IPPROTO_UDP, NULL, 5556, AF_INET, 0)) {
		g_printerr("Failure when calling eXosip_listen_addr\n");
		goto failure;
	}

	eXosip_set_user_agent(sstate->sipctx, "AirVoice");

	return 0;

failure:
	av_sip_stackteardown();
	return 1;
}

static gint av_sip_protocol_call_stage0_check_username(eXosip_event_t *e) {
	osip_from_t *from;
	osip_uri_t *uri;
	const char *username;

	from = osip_message_get_from(e->request);
	if (!from) {
		g_printerr("SIP \"From\" header was missing\n");
		return 1;
	}

	uri = osip_from_get_url(from);
	if (!uri) {
		g_printerr("URL not present in the \"From\" header\n");
		return 1;
	}

	username = osip_uri_get_username(uri);
	if (!username) {
		g_printerr("seems the URL has no username part\n");
		return 1;
	}

	if (g_strcmp0(sstate->sipconf->username, username)) {
		g_printerr("Request coming from unexpected username\n");
		return 1;
	}

	return 0;
}

static gint av_sip_protocol_call_stage0_connection_check_supported(sdp_connection_t *c, const char *rtp_port) {
	int retval = 0;
	int atoi_res;

	if ( g_strcmp0(c->c_nettype, "IN") || g_strcmp0(c->c_addrtype, "IP4") ) {
		g_printerr("Unsupported connection\n");
		return retval;
	}

	atoi_res = atoi(rtp_port);
	if ( (atoi_res >= 0) && (!(atoi_res%2)) && (atoi_res <= 65534) )
		retval = atoi_res;
	else
		g_printerr("Invalid port specified (%d)\n",atoi_res);

	return retval;
}

static gint av_sip_protocol_call_stage0_connection_setup(sdp_message_t *sdp_data, int pos_media, struct av_rtp_connection **c) {
	const char *rtp_port;
	sdp_connection_t *rtp_connection;
	int int_rtp_port;

	rtp_connection = eXosip_get_audio_connection(sdp_data);
	if (!rtp_connection) {
		g_printerr("Unable to get RTP connection data\n");
		return 1;
	}

	rtp_port = sdp_message_m_port_get(sdp_data, pos_media);
	if (!rtp_port) {
		g_printerr("Unable to get RTP port\n");
		return 1;
	}

	if ( !(int_rtp_port = av_sip_protocol_call_stage0_connection_check_supported(rtp_connection, rtp_port)) ) {
		g_print("RTP: NET TYPE=%s, ADDRESS TYPE=%s, ADDRESS=%s, PORT=%s\n",rtp_connection->c_nettype,rtp_connection->c_addrtype,rtp_connection->c_addr,rtp_port);
		return 1;
	}

	*c = av_sip_rtp_connection_alloc(rtp_connection->c_addr, int_rtp_port, sstate->sipconf->modem_audio_port);

	return 0;
}

static gint av_sip_protocol_call_stage0_check_audio_media_payload_ok(sdp_message_t *sdp_data, int pos_media, const char *payload, const char *format_desc, struct av_rtp_connection **c) {
	sdp_attribute_t *a;
	int i = 0;
	int retval = 1;
	gchar *expected_value;

	expected_value = g_strdup_printf("%s %s",payload, format_desc);

	while ( (a = sdp_message_attribute_get(sdp_data, pos_media, i)) ) {
		if (!g_strcmp0(a->a_att_field, "rtpmap") && !g_strcmp0(a->a_att_value, expected_value) ) {
			if (!av_sip_protocol_call_stage0_connection_setup(sdp_data, pos_media, c))
				retval--;
			break;
		}
		i++;
	}

	g_clear_pointer(&expected_value, g_free);

	return retval;
}

static gint av_sip_protocol_call_stage0_check_audio_media_payload(sdp_message_t *sdp_data, int pos_media, struct av_rtp_connection **c) {
	const char *payload;
	int i = 0;
	int retval = 1;

	/* If I am right, we can be sure that if payload 0 is present, it should be good for us. */
	while( (payload = sdp_message_m_payload_get(sdp_data, pos_media, i)) ) {
		g_print("Checking payload %s...\n",payload);
		if (!av_sip_protocol_call_stage0_check_audio_media_payload_ok(sdp_data, pos_media, payload, "PCMU/8000", c)) {
			retval--;
			break;
		}
		i++;
	}

	return retval;
}

static gint av_sip_protocol_call_stage0_handle_remote_sdp(eXosip_event_t *e, struct av_rtp_connection **c) {
	sdp_message_t *sdp_data;
	int i;
	const char *media_type;
	int audio_payload_ok = 0;

	sdp_data = eXosip_get_remote_sdp(sstate->sipctx, e->did);
	if (!sdp_data) {
		g_printerr("No SDP data was present\n");
		return 1;
	}

	g_print("Got SDP...\n");

	for (i=0; !sdp_message_endof_media(sdp_data,i); i++) {
		media_type = sdp_message_m_media_get(sdp_data,i);
		if (!g_strcmp0(media_type, "audio"))
			if (!av_sip_protocol_call_stage0_check_audio_media_payload(sdp_data, i, c)) {
				audio_payload_ok++;
				break;
			}
	}

	sdp_message_free(sdp_data);

	return audio_payload_ok ? 0 : 1;
}

static void av_sip_protocol_call_end_free_state(eXosip_event_t **e, struct av_rtp_connection **c, gchar **path) {
	g_clear_pointer(e, eXosip_event_free);
	av_sip_rtp_connection_free(c);
	g_clear_pointer(path, g_free);
}

static void av_sip_protocol_call_end(eXosip_event_t *e) {
	struct av_thread_cmd *exit_cmd;

	if (sstate->current_call_event) {
		if (e && (e->cid != sstate->current_call_event->cid))
			return;

		exit_cmd = av_thread_cmd(CMD_AUDIO_EXIT, NULL);
		if (exit_cmd) {
			av_thread_txcmd(sstate->audiothread, exit_cmd, 0);
			g_clear_pointer(&exit_cmd, g_free);
			sstate->poll_data[3].fd = -1;
			g_clear_pointer(&sstate->audiothread, av_thread_teardown);
		}

		av_sip_protocol_call_end_free_state(&sstate->current_call_event, &sstate->current_call_connection, &sstate->current_call_path);
	}

}

static gint av_sip_start_audio_thread(void) {
	sstate->audiothread = av_thread_setup("AudioThread", av_audiothread_startup);
	if (!sstate->audiothread)
		return 1;

	sstate->poll_data[3].fd = sstate->audiothread->sockets[0];
	sstate->poll_data[3].events = POLLIN;
	return 0;
}

/*
 * For the better or the worse, I tried to understand how things are supposed to work from here:
 * https://tools.ietf.org/html/rfc3666#section-2.1
 * and here we are at F5, so you shouldn't be "basito" yet! :)
 * Infact, eXosip2 answers for us with a "100 Trying" message to stop the other party from re-transmitting (when using something like UDP).
 *
 * Returns:
 *   non-zero when we know we're going to use our event structure; zero in any other case.
*/
static gint av_sip_protocol_call_stage0(eXosip_event_t *e) {
	struct av_rtp_connection *connection;

	if (sstate->current_call_event) {
		g_printerr("Sorry, we currently support only one incoming call at once\n");
		return 0;
	}

	/* Insecure security check: the message we received should come from the configured username. */
	if (av_sip_protocol_call_stage0_check_username(e))
		return 0;

	if (av_sip_protocol_call_stage0_handle_remote_sdp(e, &connection))
		return 0;

	g_print("RTP (%s:%d)...\n",connection->addr,connection->port);

	connection->call_direction = SIP_CALL_INCOMING;
	sstate->current_call_event = e;
	sstate->current_call_connection = connection;

	if (av_sip_start_audio_thread()) {
		av_sip_protocol_call_end(NULL);
		return 0;
	}

	return 1;
}

static gint av_sip_protocol_events(void) {
	eXosip_event_t *event;
	gint keep_event = 0;

	while ( (event = eXosip_event_wait(sstate->sipctx, 0, 0) )) {
		eXosip_lock(sstate->sipctx);

		switch(event->type) {
			case EXOSIP_REGISTRATION_SUCCESS:
				g_print("SIP registration was successful\n");
				break;
			case EXOSIP_REGISTRATION_FAILURE:
				g_printerr("SIP registration failure occurred\n");
				break;
			case EXOSIP_CALL_ACK:
				g_print("Call ACK received\n");
				break;
			case EXOSIP_CALL_CLOSED:
			case EXOSIP_CALL_CANCELLED:
			case EXOSIP_CALL_RELEASED:
				g_print("Call termination event (%d): %s\n",event->type, event->textinfo ? event->textinfo : "no event text");
				av_sip_protocol_call_end(event);
				break;
			case EXOSIP_CALL_INVITE:
				g_print("SIP INVITE received\n");
				keep_event = av_sip_protocol_call_stage0(event);
				break;
			default:
				g_printerr("Unknown SIP event (%d): %s\n",event->type, event->textinfo ? event->textinfo : "no event text");
				break;
		}

		eXosip_unlock(sstate->sipctx);

		if (!keep_event)
			g_clear_pointer(&event, eXosip_event_free);
	}

	return 0;
}

static gint av_sip_stackconfig(struct av_modem_config *mc) {
	osip_message_t *regmsg;
	int send_reg_retval;

	if (eXosip_add_authentication_info(sstate->sipctx, mc->username, mc->username, mc->password, NULL, NULL)) {
		g_printerr("Failure adding authentication infos\n");
		return 1;
	}

	eXosip_lock(sstate->sipctx);
	sstate->reg_id = eXosip_register_build_initial_register(sstate->sipctx, mc->sip_id, mc->sip_host, NULL, 200, &regmsg);
	if (sstate->reg_id < 1) {
		g_printerr("Failure building initial SIP registration message\n");
		eXosip_unlock(sstate->sipctx);
		return 1;
	}

	send_reg_retval = eXosip_register_send_register(sstate->sipctx, sstate->reg_id, regmsg);
	eXosip_unlock(sstate->sipctx);

	if (send_reg_retval) {
		g_printerr("Failure sending SIP REGISTER\n");
		return 1;
	}

	sstate->sipconf = mc;

	return 0;
}

static gint av_sip_regconf(struct av_thread_cmd *cmd) {
	struct av_modem_config *sipconf = cmd->payload;
	gint retval = 1;

	if (sipconf->username && sipconf->password && sipconf->sip_host && sipconf->sip_id && sipconf->modem_audio_port && sipconf->sip_local_ip_addr) {
		if ( (retval = av_sip_stackconfig(sipconf)) )
			av_config_free(&sipconf);
	}
	else
		av_config_free(&sipconf);

	return retval;
}

static void av_sip_core_poll_setup(void) {
	sstate->poll_data[0].fd = sstate->self->sockets[1];
	sstate->poll_data[0].events = POLLIN;
}

static gint av_sip_timerfd_setup(void) {
	sstate->poll_data[2].fd = timerfd_create(CLOCK_MONOTONIC, 0);
	if (sstate->poll_data[2].fd < 0) {
		g_printerr("timerfd_create: %s\n",strerror(errno));
		return 1;
	}

	sstate->poll_data[2].events = POLLIN;
	sstate->automatic_action_timer.it_value.tv_sec = 1;
	sstate->automatic_action_timer.it_value.tv_nsec = 0;
	sstate->automatic_action_timer.it_interval.tv_sec = 5;
	sstate->automatic_action_timer.it_interval.tv_nsec = 0;

	if (timerfd_settime(sstate->poll_data[2].fd, 0, &sstate->automatic_action_timer, NULL)) {
		g_printerr("timerfd_settime: %s\n",strerror(errno));
		return 1;
	}

	return 0;
}

static void av_sip_timerfd_teardown(void) {
	/*
	 * This should not happen, because we're not calling this function if timerfd init fails.
	 * Still, it seems more efficient to avoid trying to close an fd with value -1, a bad fd, even tough this won't have
	 * consequences probably.
	*/
	if (sstate->poll_data[2].fd < 0)
		return;

	if (close(sstate->poll_data[2].fd))
		g_printerr("Failure closing SIP events socket: %s\n",strerror(errno));

	sstate->poll_data[2].fd = -1;
}

/*
 * You will find some comments along this function, describing the conclusions I arrived to while trying to understand the way
 * things might go wrong here. However, I guess those details may be susceptible to changes in OSIP.
*/
static gint av_sip_protocol_call_build_sdp(osip_message_t *a, int local_port, sdp_message_t **answer_sdp_message) {
	sdp_message_t *sdpm;
	int retval = 0;
	gchar *session_id;
	gchar *session_version;
	gchar *port_str;
	gchar *media_type_audio = g_strdup("audio");
	gchar *media_rtp_profile = g_strdup("RTP/AVP");
	gchar *sdp_nettype = g_strdup("IN");
	gchar *sdp_addrtype = g_strdup("IP4");
	gchar *sdp_addr = g_strdup(sstate->sipconf->sip_local_ip_addr);
	gchar *rtpmap0_field = g_strdup("rtpmap");
	gchar *rtpmap0_value = g_strdup("0 PCMU/8000");

	/*
	 * This function might return OSIP_NOMEM if a call to osip_malloc or osip_list_init fails.
	*/
	if (sdp_message_init(&sdpm))
		return ++retval;

	/* This won't return OSIP_BADPARAMETER, because we know sdpm isn't NULL. */
	sdp_message_v_version_set(sdpm, g_strdup("0"));

	session_id = g_strdup_printf("%ld",random());
	session_version = g_strdup_printf("%ld",random());
	port_str = g_strdup_printf("%d",local_port);

	/* This won't return OSIP_BADPARAMETER as well, because we know sdpm isn't NULL. */
	sdp_message_o_origin_set(sdpm, g_strdup("airvoice"), session_id, session_version, g_strdup(sdp_nettype), g_strdup(sdp_addrtype), g_strdup(sdp_addr));

	/* Returns -1 if sdpm is NULL. But it shouldn't be. */
	sdp_message_s_name_set(sdpm, g_strdup("DongleCall"));

	/*
	 * Add the classic payload 0 audio media.
	 *
	 * Note: here sdp_media_init gets called, which in turns calls osip_malloc and osip_list_init. This can definitely fail.
	*/
	if (sdp_message_m_media_add(sdpm, media_type_audio, port_str, NULL, media_rtp_profile)) {
		g_printerr("Failure adding SDP payload 0 media\n");
		retval++;
		goto out;
	}
	else
		media_rtp_profile = media_type_audio = port_str = NULL;

	/* -1 = global c= field */
	if (sdp_message_c_connection_add(sdpm, -1, sdp_nettype, sdp_addrtype, sdp_addr, NULL, NULL)) {
		g_printerr("Failure adding SDP connection\n");
		retval++;
		goto out;
	}
	else
		sdp_nettype = sdp_addrtype = sdp_addr = NULL;

	/* Fails if sdpm is NULL. */
	sdp_message_m_payload_add(sdpm, 0, g_strdup("0"));

	if (sdp_message_a_attribute_add(sdpm, 0, rtpmap0_field, rtpmap0_value)) {
		g_print("Failure adding %s attribute\n",rtpmap0_field);
		retval++;
		goto out;
	}
	else
		rtpmap0_field = rtpmap0_value = NULL;

out:

	if (retval) {
		g_clear_pointer(&port_str, g_free);
		g_clear_pointer(&media_type_audio, g_free);
		g_clear_pointer(&media_rtp_profile, g_free);
		g_clear_pointer(&sdp_nettype, g_free);
		g_clear_pointer(&sdp_addrtype, g_free);
		g_clear_pointer(&sdp_addr, g_free);
		g_clear_pointer(&rtpmap0_field, g_free);
		g_clear_pointer(&rtpmap0_value, g_free);
		g_clear_pointer(&sdpm, sdp_message_free);
	}

	*answer_sdp_message = sdpm;

	return retval;
}

/*
 * Welcome to the F7 stage of the call, where we send a 183 message, to which we append an SDP description to allow for early
 * media.
*/
static gint av_sip_protocol_call_stage1(int rtp_local_port) {
	osip_message_t *answer;
	gint retval = 0;
	sdp_message_t *sdpm = NULL;
	char *sdp_string = NULL;

	if (eXosip_call_build_answer(sstate->sipctx, sstate->current_call_event->tid, 183, &answer)) {
		g_printerr("Failure building answer\n");
		return ++retval;
	}

	if (av_sip_protocol_call_build_sdp(answer, rtp_local_port, &sdpm)) {
		g_printerr("Failure building SDP\n");
		retval++;
		goto out;
	}

	if (sdp_message_to_str(sdpm, &sdp_string)) {
		g_printerr("Failure building SDP message string\n");
		retval++;
		goto out;
	}

	if (osip_message_set_content_type(answer, "application/sdp")) {
		g_printerr("Failure setting answer message content type\n");
		retval++;
		goto out;
	}

	if (osip_message_set_body(answer, sdp_string, strlen(sdp_string))) {
		g_printerr("Failure attaching SDP to answer\n");
		retval++;
		goto out;
	}

	if (eXosip_call_send_answer(sstate->sipctx, sstate->current_call_event->tid, 183, answer)) {
		g_printerr("Failure sending answer\n");
		retval++;
		goto out;
	}

out:
	if (retval)
		g_clear_pointer(&answer, osip_message_free);

	g_clear_pointer(&sdpm, sdp_message_free);
	g_clear_pointer(&sdp_string, g_free);

	return retval;
}

static gint av_sip_core_msg(void) {
	struct av_thread_cmd *cmd;
	gint retval = 0;

	cmd = av_thread_rxcmd(sstate->self, 1);
	if (!cmd)
		return retval;

	switch(cmd->msgtype) {
		case SIP_CMD_EXIT:
			g_print("SIP thread exiting...\n");
			retval++;
			break;
		case SIP_CMD_REGISTER:
			retval = av_sip_regconf(cmd);
			break;
		case SIP_CMD_CALL_IN_PROGRESS:
			sstate->current_call_path = cmd->payload;
			g_print("Call @ %s\n",sstate->current_call_path);
			retval = av_sip_protocol_call_stage1(sstate->local_rtp_port);
			break;
		default:
			g_printerr("Unknown command received (%d)!\n",cmd->msgtype);
			retval++;
			break;
	}

	g_clear_pointer(&cmd, g_free);

	return retval;
}

static const char *av_sip_protocol_call_stage0_extract_dest_number(osip_message_t *req) {

	/*
	 * Can this happen?
	*/
	if (!req->req_uri) {
		g_print("Request contained no URI; please report this back.\n");
		return NULL;
	}

	return osip_uri_get_username(req->req_uri);
}

static gint av_sip_audio_msg(void) {
	struct av_thread_cmd *cmd;
	struct av_thread_cmd *call_cmd;
	gint retval = 0;
	const char *dest_number;
	int *rtp_local_port;

	cmd = av_thread_rxcmd(sstate->audiothread, 0);
	if (!cmd)
		return retval;

	switch(cmd->msgtype) {
		case AUDIO_EVENT_READY:
			g_print("Audio thread talks to us! :)\nWill the dongle be with us?\n");
			call_cmd = av_thread_cmd(CMD_AUDIO_INIT, sstate->current_call_connection);
			if (call_cmd) {
				av_thread_txcmd(sstate->audiothread, call_cmd, 0);
				g_clear_pointer(&call_cmd, g_free);
			}
			break;
		case AUDIO_EVENT_RTP_OK:
			g_print("Audio init OK\n");
			rtp_local_port = cmd->payload;
			sstate->local_rtp_port = *rtp_local_port;
			g_clear_pointer(&rtp_local_port, g_free);
			dest_number = av_sip_protocol_call_stage0_extract_dest_number(sstate->current_call_event->request);
			if (!dest_number)
				break;

			call_cmd = av_thread_cmd(SIP_EVENT_INCOMING_CALL, NULL);
			if (call_cmd) {
				call_cmd->payload = g_strdup(dest_number);
				av_thread_txcmd(sstate->self, call_cmd, 1);
				g_clear_pointer(&call_cmd, g_free);
			}
			break;
		default:
			g_printerr("Unknown audio event received (%d)!\n",cmd->msgtype);
			retval++;
			break;
	}

	g_clear_pointer(&cmd, g_free);

	return retval;
}

static gint av_sip_protocol_automatic_action(void) {
	uint64_t n_expirations;

	(void) read(sstate->poll_data[2].fd, &n_expirations, sizeof n_expirations);
	if (n_expirations > 1)
		g_printerr("WARNING: %lu event(s) missed\n",(n_expirations-1));

	eXosip_lock(sstate->sipctx);
	eXosip_automatic_action(sstate->sipctx);
	eXosip_unlock(sstate->sipctx);

	return 0;
}

static gint av_sip_loop(void) {
	gint n_events;

	n_events = poll(sstate->poll_data, 4, -1);
	if (n_events < 0) {
		g_printerr("Failure while poll()ing: %s\n",strerror(errno));
		return -1;
	}

	/* Did we get a message from AV main thread? */
	if (sstate->poll_data[0].revents == POLLIN) {
		sstate->poll_data[0].revents = 0;
		return av_sip_core_msg();
	}

	/* ... A SIP-related event? */
	if (sstate->poll_data[1].revents == POLLIN) {
		sstate->poll_data[1].revents = 0;
		return av_sip_protocol_events();
	}

	/* ... timerfd? */
	if (sstate->poll_data[2].revents == POLLIN) {
		sstate->poll_data[2].revents = 0;
		return av_sip_protocol_automatic_action();
	}

	/* or audio thread? */
	if (sstate->poll_data[3].revents == POLLIN) {
		sstate->poll_data[3].revents = 0;
		return av_sip_audio_msg();
	}

	return 0;
}

void *av_sip_init(gpointer data) {
	struct av_thread *t = data;
	struct av_thread_cmd *ready;

	sstate = g_try_malloc0(sizeof *sstate);
	if (!sstate) {
		g_printerr("Failure while allocating SIP state!\n");
		return sstate;
	}

	sstate->self = t;

	if (av_sip_stacksetup())
		goto out;

	if (av_sip_timerfd_setup())
		goto out_notimerfd;

	/* Inform core we are ready to proceed. */
	ready = av_thread_cmd(SIP_EVENT_READY, NULL);
	if (ready) {
		av_thread_txcmd(t, ready, 1);
		g_clear_pointer(&ready, g_free);
	}

	/* Setup poll-based communications with AV thread. */
	av_sip_core_poll_setup();

	/* do poll() */
	while(!av_sip_loop());

	av_sip_protocol_call_end(NULL);

	g_print("SIP: BYE BYE!\n");

	av_sip_timerfd_teardown();

out_notimerfd:
	av_sip_stackteardown();

out:
	g_clear_pointer(&sstate, g_free);

	return sstate;
}

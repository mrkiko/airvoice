// SPDX-License-Identifier: GPL-2.0-or-later

/* System haders */
#include <poll.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* AV headers */
#include <av.h>
#include <av_audio.h>
#include <av_sip.h>
#include <av_thread.h>
#include <av_threadcomm.h>

#define AV_AUDIO_POLL_NUM_FDS 2
/*
 * The following #define is also a tribute to the Wys project, found at
 * https://source.puri.sm/Librem5/wys
*/
#define TTY_CHUNK_SIZE   320

struct av_audio_state {
	struct av_thread *self;
	struct pollfd poll_data[AV_AUDIO_POLL_NUM_FDS];
	RtpSession *session;
	uint32_t user_ts;
} *astate;

void av_audio_astate_free(void) {
	g_clear_pointer(&astate, g_free);
}

static gint av_audio_astate_alloc(void) {
	int i;

	astate = g_try_malloc0(sizeof *astate);
	if (!astate) {
		g_printerr("Failure allocating audio state\n");
		return 1;
	}

	for (i=0;i<AV_AUDIO_POLL_NUM_FDS;i++)
		astate->poll_data[i].fd = -1;

	return 0;
}

static gint av_audio_close_fd(int fd) {
	int retval = 1;

	if (fd < 0)
		return retval;

	g_print("Closing FD %d...\n",fd);

	if (close(fd))
		g_printerr("Error while closing serial FD: %s\n",strerror(errno));
	else
		retval--;

	return retval;
}

static gint av_audio_serial_init(const gchar *device) {
	int fd;
	struct termios term_attr;
	int fd_flags;
	int retval = 0;

	/*
	 * First of all, we try to get a file descriptor for our serial device.
	 * We pass these flags:
	 * - O_RDWR: opens file read/write
	 * - O_NOCTTY: prevent this terminal device from becoming our controlling one,
	 *   even in the case we don't have another.
	 * - O_NONBLOCK: non-blocking IO.
	*/
	fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0) {
		g_printerr("Unable to get a file descriptor for %s: %s\n",device,strerror(errno));
		return ++retval;
	}

	/* Is this really a serial port? */
	if (!isatty(fd)) {
		g_printerr("%s does not look like a valid serial port: %s\n",device,strerror(errno));
		retval++;
		goto out;
	}

	fd_flags = fcntl(fd, F_GETFD);
	if (fd_flags < 0) {
		g_printerr("Unable to get serial FD flags: %s\n",strerror(errno));
		retval++;
		goto out;
	}

	if (fcntl(fd, F_SETFD, fd_flags | FD_CLOEXEC) == -1) {
		g_printerr("Error setting serial FD flags: %s\n",strerror(errno));
		retval++;
		goto out;
	}

	if (tcgetattr(fd, &term_attr)) {
		g_printerr("Failure getting terminal attributes for %s: %s\n",device,strerror(errno));
		retval++;
		goto out;
	}

	/*
	 * Set up control modes as follows:
	 * - B115200: serial port baudrate
	 * - CS8: character size
	 * - CREAD: the man page says "enable receiver" ... ??
	 * - CRTSCTS: enable RTS/CTS  (hardware)  flow  control
	 *
	 * Note: this code may be replaced by a call to cfmakeraw().
	*/
	term_attr.c_cflag = B115200 | CS8 | CREAD | CRTSCTS;

	/* Input modes: disable everything ? */
	term_attr.c_iflag = 0;

	/* Output modes: disable everything ? */
	term_attr.c_oflag = 0;

	/* Local modes: disable everything ? */
	term_attr.c_lflag = 0;

	/* Sets minimum number of characters for noncanonical read (MIN). */
	term_attr.c_cc[VMIN] = 1;

	/*
	 * Timeout in deciseconds for noncanonical read (TIME). Or, in other words, I
	 * guess this means: give us data as soon as it's there.
	*/
	term_attr.c_cc[VTIME] = 0;

	/*
	 * Set terminal attributes. From TERMIOS(3):
	 * TCSAFLUSH: the change occurs after all output written to the object
	 * referred by fd has been transmitted, and all input that  has  been
	 * received but not read will be discarded before the change is made.
	*/
	if (tcsetattr(fd, TCSAFLUSH, &term_attr)) {
		g_printerr("Failure setting terminal attributes for %s: %s\n",device,strerror(errno));
		retval++;
		goto out;
	}

out:
	if (retval)
		av_audio_close_fd(fd);
	else
		astate->poll_data[1].fd = fd;

	return retval;
}

static int *av_audio_rtp_get_local_port(void) {
	int *rtp_port;

	rtp_port = g_try_malloc0(sizeof *rtp_port);
	if (!rtp_port)
		return rtp_port;

	*rtp_port = rtp_session_get_local_port(astate->session);

	return rtp_port;
}

static int av_audio_rtp_init(const char *ip, int port) {

	ortp_init();
	ortp_scheduler_init();

	//ortp_set_log_level_mask(NULL, ORTP_MESSAGE|ORTP_WARNING|ORTP_ERROR);
	ortp_set_log_level_mask(NULL, ORTP_DEBUG|ORTP_MESSAGE|ORTP_WARNING|ORTP_ERROR);

	astate->session = rtp_session_new(RTP_SESSION_SENDRECV);
	if (!astate->session) {
		g_printerr("RTP session init failed\n");
		return 1;
	}

	rtp_session_set_scheduling_mode(astate->session,0);
	rtp_session_set_blocking_mode(astate->session,0);
	rtp_session_set_connected_mode(astate->session,TRUE);
	rtp_session_set_remote_addr(astate->session,ip,port);
	rtp_session_set_payload_type(astate->session,0);

	return 0;
}

static int av_audio_do_serial_read(int fd) {
	ssize_t nbytes;
	size_t missing = (TTY_CHUNK_SIZE/2);
	unsigned char audiobuf[TTY_CHUNK_SIZE/2];
	unsigned char *bufptr = audiobuf;
	int retries = 0;

	if (!astate->user_ts)
		g_print("Serial read...\n");

	while (missing && (retries < 10) ) {
		retries++;
		nbytes = read(fd, bufptr, missing);
		if (nbytes < 0) {
			if (errno == EAGAIN)
				continue;
			g_printerr("Error reading from serial device: %s\n",strerror(errno));
			break;
		}
		missing -= nbytes;
		bufptr += nbytes;
	}

	if (retries > 10)
		g_print("Could happen, retries = %d\n",retries);

	rtp_session_send_with_ts(astate->session, audiobuf, nbytes, astate->user_ts);
	astate->user_ts += nbytes;

	return 0;
}

static gint av_audio_sip_msg(void) {
	struct av_thread_cmd *cmd;
	gint retval = 0;
	const struct av_rtp_connection *pbx_connection;
	struct av_thread_cmd *acmd;

	cmd = av_thread_rxcmd(astate->self, 1);

	if (!cmd)
		return retval;

	switch(cmd->msgtype) {
		case CMD_AUDIO_INIT:
			g_print("Attempting audio init\n");
			pbx_connection = cmd->payload;

			if (av_audio_rtp_init(pbx_connection->addr, pbx_connection->port)) {
				retval++;
				break;
			}

			g_print("Attempting serial init, even tough %s is NULL\n",pbx_connection->serial_device);
			if (av_audio_serial_init(pbx_connection->serial_device)) {
				retval++;
				break;
			}

			acmd = av_thread_cmd(AUDIO_EVENT_RTP_OK, NULL);
			if (acmd) {
				acmd->payload = av_audio_rtp_get_local_port();

				if (acmd->payload) {
					av_thread_txcmd(astate->self, acmd, 1);
					g_print("Answered that AUDIO_EVENT_RTP_OK\n");
				}

				g_clear_pointer(&acmd, g_free);
			}

			break;
		case CMD_AUDIO_EXIT:
			retval++;
			break;
		default:
			g_printerr("Unknown command received (%d)!\n",cmd->msgtype);
			retval++;
			break;
	}

	g_clear_pointer(&cmd, g_free);

	return retval;
}

static gint av_audio_do_poll(void) {
	gint n_events;

	n_events = poll(astate->poll_data, AV_AUDIO_POLL_NUM_FDS, -1);
	if (n_events < 0) {
		g_printerr("Failure while poll()ing: %s\n",strerror(errno));
		return -1;
	}

	/* SipStack is telling something to us... */
	if (astate->poll_data[0].revents == POLLIN) {
		astate->poll_data[0].revents = 0;
		return av_audio_sip_msg();
	}

	/* We could read from serial... */
	if (astate->poll_data[1].revents == POLLIN) {
		astate->poll_data[1].revents = 0;
		return av_audio_do_serial_read(astate->poll_data[1].fd);
	}

	return 0;
}

static void av_audio_rtp_deinit(void) {
	astate->user_ts = 0;
	g_clear_pointer(&astate->session, rtp_session_destroy);
	ortp_exit();
	ortp_global_stats_display();
}

static void av_audio_poll_init(void) {
	int i;

	for (i=0;i<AV_AUDIO_POLL_NUM_FDS;i++) {
		astate->poll_data[i].events = POLLIN;
	}

	astate->poll_data[0].fd = astate->self->sockets[1];
}

void *av_audiothread_startup(gpointer data) {
	struct av_thread *t = data;
	struct av_thread_cmd *ready;

	if (av_audio_astate_alloc())
		return astate;

	astate->self = t;

	ready = av_thread_cmd(AUDIO_EVENT_READY, NULL);
	if (ready) {
		av_thread_txcmd(t, ready, 1);
		g_clear_pointer(&ready, g_free);
	}

	av_audio_poll_init();

	/* do poll() */
	while(!av_audio_do_poll());

	g_print("Audio thread exiting...\n");

	av_audio_rtp_deinit();
	av_audio_close_fd(astate->poll_data[1].fd);
	av_audio_astate_free();
	return NULL;
}

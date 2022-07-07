// SPDX-License-Identifier: GPL-2.0-or-later

/* libconfig header */
#include <libconfig.h>

/* AV headers */
#include <av_config.h>

static void av_config_deinit(config_t **c) {
	config_destroy(*c);
	g_clear_pointer(c, g_free);
}

static struct config_t *av_config_init(const gchar *filename) {
	config_t *lc_context;
	const gchar *error_file;
	const gchar *error_text;

	lc_context = g_try_malloc0(sizeof *lc_context);
	if (!lc_context) {
		g_printerr("Failure allocating libconfig context\n");
		return lc_context;
	}

	config_init(lc_context);
	config_set_option(lc_context, CONFIG_OPTION_AUTOCONVERT, CONFIG_FALSE);

	if (config_read_file(lc_context, filename) != CONFIG_TRUE) {
		error_file = config_error_file(lc_context);
		error_text = config_error_text(lc_context);
		g_printerr("Failure parsing %s configuration file: %s (line %d)\n",error_file ? error_file : "main",error_text ? error_text : "unknown error",config_error_line(lc_context));
		goto failure;
	}

	return lc_context;

failure:
	av_config_deinit(&lc_context);
	return lc_context;
}

static gchar *av_config_search(config_t *l, const gchar *base, const gchar *value) {
	gchar *config_path;
	gchar *result = NULL;
	const gchar *config_value;

	config_path = g_strdup_printf("MM_%s.%s", base, value);
	if (config_lookup_string(l, config_path, &config_value) == CONFIG_TRUE)
		result = g_strdup(config_value);

	g_clear_pointer(&config_path, g_free);

	return result;
}

static struct av_modem_config *av_config_extract_data(AvModem *m, config_t *lc) {
	struct av_modem_config *mc = NULL;
	const gchar *equipment_id;
	MMModem *modem;

	modem = avmodem_get_mmmodem(m);
	g_assert(modem);

	equipment_id = mm_modem_get_equipment_identifier(modem);
	if (!equipment_id) {
		g_printerr("No equipment ID for this modem; unable to determine it's configuration data!\n");
		goto failure;
	}

	mc = g_try_malloc0(sizeof *mc);
	if (!mc) {
		g_printerr("Failure allocating AVModem config structure\n");
		goto failure;
	}

	mc->username = av_config_search(lc, equipment_id, "username");
	mc->password = av_config_search(lc, equipment_id, "password");
	mc->sip_host = av_config_search(lc, equipment_id, "sip_host");
	mc->sip_id = av_config_search(lc, equipment_id, "sip_id");
	mc->modem_audio_port = av_config_search(lc, equipment_id, "audio_port");
	mc->sip_local_ip_addr = av_config_search(lc, equipment_id, "local_ip");

	return mc;

failure:
	av_config_free(&mc);
	return mc;
}

struct av_modem_config *av_config_parse(AvModem *m) {
	config_t *lc;
	struct av_modem_config *mc = NULL;

	lc = av_config_init("AirVoice.cfg");
	if (lc) {
		mc = av_config_extract_data(m, lc);
		av_config_deinit(&lc);
	}

	return mc;
}

void av_config_free(struct av_modem_config **c) {
	if (*c) {
		g_clear_pointer(&(*c)->username, g_free);
		g_clear_pointer(&(*c)->password, g_free);
		g_clear_pointer(&(*c)->sip_host, g_free);
		g_clear_pointer(&(*c)->sip_id, g_free);
		g_clear_pointer(&(*c)->modem_audio_port, g_free);
		g_clear_pointer(&(*c)->sip_local_ip_addr, g_free);
		g_clear_pointer(c, g_free);
	}

	return;
}

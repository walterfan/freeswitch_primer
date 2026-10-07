#include <switch.h>
#include "mod_tutorial_logic.h"

#include <stdlib.h>
#include <strings.h>
#include <string.h>

#define TUTORIAL_EVENT "tutorial::ivr_choice"
#define TUTORIAL_CONFIG_FILE "mod_tutorial.conf"

static struct {
	switch_memory_pool_t *pool;
	switch_mutex_t *mutex;
	switch_time_t started_at;
	tutorial_counter_state_t counters;
	switch_bool_t event_reserved;
} globals;

static void cleanup_tutorial_resources(void)
{
	if (globals.event_reserved) {
		switch_event_free_subclass(TUTORIAL_EVENT);
		globals.event_reserved = SWITCH_FALSE;
	}
	if (globals.mutex) {
		switch_mutex_destroy(globals.mutex);
		globals.mutex = NULL;
	}
}

static switch_status_t load_tutorial_config(void)
{
	switch_xml_t xml = NULL, cfg = NULL, menus = NULL, menu = NULL, choice = NULL;
	tutorial_counter_state_t loaded;
	uint32_t loaded_count = 0;
	uint32_t menu_count = 0;

	memset(&loaded, 0, sizeof(loaded));
	if (!(xml = switch_xml_open_cfg(TUTORIAL_CONFIG_FILE, &cfg, NULL))) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
			"mod_tutorial: unable to load %s\n", TUTORIAL_CONFIG_FILE);
		return SWITCH_STATUS_FALSE;
	}
	if (!(menus = switch_xml_child(cfg, "menus"))) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
			"mod_tutorial: configuration requires a menus element\n");
		switch_xml_free(xml);
		return SWITCH_STATUS_FALSE;
	}

	for (menu = switch_xml_child(menus, "menu"); menu; menu = menu->next) {
		const char *menu_name = switch_xml_attr_soft(menu, "name");
		switch_bool_t menu_has_choice = SWITCH_FALSE;

		if (++menu_count > TUTORIAL_MAX_MENUS || !tutorial_valid_menu(menu_name)) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
				"mod_tutorial: invalid or excessive menu configuration\n");
			switch_xml_free(xml);
			return SWITCH_STATUS_FALSE;
		}
		{
			uint32_t previous_index;
			for (previous_index = 0; previous_index < loaded_count; previous_index++) {
				if (!strcmp(loaded.choices[previous_index].menu, menu_name)) {
					switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
						"mod_tutorial: duplicate menu configuration\n");
					switch_xml_free(xml);
					return SWITCH_STATUS_FALSE;
				}
			}
		}
		for (choice = switch_xml_child(menu, "choice"); choice; choice = choice->next) {
			const char *digit = switch_xml_attr_soft(choice, "digit");
			if (loaded_count >= TUTORIAL_MAX_CHOICES || !tutorial_valid_choice(digit)) {
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
					"mod_tutorial: invalid or excessive choice configuration\n");
				switch_xml_free(xml);
				return SWITCH_STATUS_FALSE;
			}
			{
				uint32_t duplicate_index;
				for (duplicate_index = 0; duplicate_index < loaded_count; duplicate_index++) {
					if (!strcmp(loaded.choices[duplicate_index].menu, menu_name) &&
						!strcmp(loaded.choices[duplicate_index].choice, digit)) {
						switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
							"mod_tutorial: duplicate menu and choice\n");
						switch_xml_free(xml);
						return SWITCH_STATUS_FALSE;
					}
				}
			}
			switch_copy_string(loaded.choices[loaded_count].menu, menu_name,
				sizeof(loaded.choices[loaded_count].menu));
			switch_copy_string(loaded.choices[loaded_count].choice, digit,
				sizeof(loaded.choices[loaded_count].choice));
			loaded_count++;
			menu_has_choice = SWITCH_TRUE;
		}
		if (!menu_has_choice) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
				"mod_tutorial: every menu requires at least one choice\n");
			switch_xml_free(xml);
			return SWITCH_STATUS_FALSE;
		}
	}
	switch_xml_free(xml);
	if (!loaded_count) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
			"mod_tutorial: at least one menu choice is required\n");
		return SWITCH_STATUS_FALSE;
	}

	loaded.choice_count = loaded_count;
	switch_mutex_lock(globals.mutex);
	globals.counters = loaded;
	switch_mutex_unlock(globals.mutex);
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t tutorial_metrics_api(
	const char *cmd, switch_core_session_t *session, switch_stream_handle_t *stream)
{
	char *input = NULL;
	char *argv[2] = { 0 };
	int argc = 0;
	tutorial_counter_state_t snapshot;
	uint64_t uptime;
	char output[8192];

	if (!zstr(cmd)) {
		input = strdup(cmd);
		argc = switch_separate_string(input, ' ', argv, 2);
	}
	if (argc > 1 || (argc == 1 && strcasecmp(argv[0], "text") &&
		strcasecmp(argv[0], "json"))) {
		stream->write_function(stream, "-USAGE: tutorial_metrics [text|json]\n");
		switch_safe_free(input);
		return SWITCH_STATUS_SUCCESS;
	}

	switch_mutex_lock(globals.mutex);
	snapshot = globals.counters;
	uptime = (uint64_t)((switch_micro_time_now() - globals.started_at) / 1000000);
	switch_mutex_unlock(globals.mutex);

	tutorial_format_metrics(&snapshot, uptime,
		argc == 1 && !strcasecmp(argv[0], "json"), output, sizeof(output));
	stream->write_function(stream, "%s", output);
	switch_safe_free(input);
	return SWITCH_STATUS_SUCCESS;
}

static void tutorial_ivr_metric_app(switch_core_session_t *session, const char *data)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	char *input;
	char *argv[3] = { 0 };
	int argc;
	int selected_index = -1;
	char selected_menu[TUTORIAL_MAX_MENU_LENGTH + 1] = { 0 };
	char selected_choice[2] = { 0 };
	const char *uuid;
	switch_time_t now;
	switch_event_t *event = NULL;

	switch_channel_set_variable(channel, "tutorial_ivr_recorded", "false");
	switch_channel_set_variable(channel, "tutorial_ivr_menu", NULL);
	switch_channel_set_variable(channel, "tutorial_ivr_choice", NULL);
	switch_channel_set_variable(channel, "tutorial_ivr_error", NULL);
	input = switch_core_session_strdup(session, data ? data : "");
	argc = switch_separate_string(input, ' ', argv, 3);

	switch_mutex_lock(globals.mutex);
	if (argc == 2) {
		selected_index = tutorial_find_choice(&globals.counters, argv[0], argv[1]);
	}
	globals.counters.invocations++;
	if (selected_index < 0) {
		globals.counters.invalid++;
	} else {
		globals.counters.choices[selected_index].count++;
		switch_copy_string(selected_menu, globals.counters.choices[selected_index].menu,
			sizeof(selected_menu));
		switch_copy_string(selected_choice, globals.counters.choices[selected_index].choice,
			sizeof(selected_choice));
	}
	switch_mutex_unlock(globals.mutex);

	if (selected_index < 0) {
		switch_channel_set_variable(channel, "tutorial_ivr_error", "invalid_menu_or_choice");
		switch_log_printf(SWITCH_CHANNEL_CHANNEL_LOG(channel), SWITCH_LOG_WARNING,
			"mod_tutorial rejected tutorial_ivr_metric input\n");
		return;
	}

	switch_channel_set_variable(channel, "tutorial_ivr_menu", selected_menu);
	switch_channel_set_variable(channel, "tutorial_ivr_choice", selected_choice);
	switch_channel_set_variable(channel, "tutorial_ivr_recorded", "true");
	uuid = switch_core_session_get_uuid(session);
	now = switch_micro_time_now();
	if (switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TUTORIAL_EVENT) ==
		SWITCH_STATUS_SUCCESS) {
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Menu", selected_menu);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Choice", selected_choice);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Unique-ID", uuid);
		switch_event_add_header(event, SWITCH_STACK_BOTTOM, "Event-Date-Timestamp",
			"%" SWITCH_TIME_T_FMT, now);
		switch_event_fire(&event);
	}
}

SWITCH_MODULE_LOAD_FUNCTION(mod_tutorial_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_tutorial_shutdown);
SWITCH_MODULE_DEFINITION(mod_tutorial, mod_tutorial_load, mod_tutorial_shutdown, NULL);

SWITCH_MODULE_LOAD_FUNCTION(mod_tutorial_load)
{
	switch_api_interface_t *api_interface;
	switch_application_interface_t *app_interface;

	memset(&globals, 0, sizeof(globals));
	globals.pool = pool;
	globals.started_at = switch_micro_time_now();
	if (switch_mutex_init(&globals.mutex, SWITCH_MUTEX_NESTED, pool) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
			"mod_tutorial: unable to initialize synchronization\n");
		return SWITCH_STATUS_TERM;
	}
	if (load_tutorial_config() != SWITCH_STATUS_SUCCESS) {
		cleanup_tutorial_resources();
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(TUTORIAL_EVENT) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
			"mod_tutorial: unable to reserve event subclass\n");
		cleanup_tutorial_resources();
		return SWITCH_STATUS_TERM;
	}
	globals.event_reserved = SWITCH_TRUE;

	*module_interface = switch_loadable_module_create_module_interface(pool, modname);
	SWITCH_ADD_API(api_interface, "tutorial_metrics", "Tutorial Metrics snapshot",
		tutorial_metrics_api, "tutorial_metrics [text|json]");
	SWITCH_ADD_APP(app_interface, "tutorial_ivr_metric", "Record tutorial IVR choice",
		"Validate and record a tutorial IVR choice", tutorial_ivr_metric_app,
		"<menu> <choice>", SAF_NONE);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
		"mod_tutorial loaded with %u bounded choices\n", globals.counters.choice_count);
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_tutorial_shutdown)
{
	cleanup_tutorial_resources();
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_tutorial unloaded\n");
	return SWITCH_STATUS_SUCCESS;
}

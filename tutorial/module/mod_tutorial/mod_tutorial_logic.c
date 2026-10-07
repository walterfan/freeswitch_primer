#include "mod_tutorial_logic.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

int tutorial_valid_menu(const char *menu)
{
	size_t length;
	const char *cursor;

	if (!menu || !menu[0]) {
		return 0;
	}
	length = strlen(menu);
	if (length > TUTORIAL_MAX_MENU_LENGTH) {
		return 0;
	}
	for (cursor = menu; *cursor; cursor++) {
		if (!(('a' <= *cursor && *cursor <= 'z') ||
			('A' <= *cursor && *cursor <= 'Z') ||
			('0' <= *cursor && *cursor <= '9') ||
			*cursor == '_' || *cursor == '-')) {
			return 0;
		}
	}
	return 1;
}

int tutorial_valid_choice(const char *choice)
{
	return choice && choice[0] && !choice[1] &&
		(('0' <= choice[0] && choice[0] <= '9') ||
		 choice[0] == '*' || choice[0] == '#');
}

int tutorial_find_choice(const tutorial_counter_state_t *state,
	const char *menu, const char *choice)
{
	uint32_t index;

	if (!state || !tutorial_valid_menu(menu) || !tutorial_valid_choice(choice)) {
		return -1;
	}
	for (index = 0; index < state->choice_count; index++) {
		if (!strcmp(state->choices[index].menu, menu) &&
			!strcmp(state->choices[index].choice, choice)) {
			return (int)index;
		}
	}
	return -1;
}

int tutorial_record_choice(tutorial_counter_state_t *state,
	const char *menu, const char *choice)
{
	const int index = tutorial_find_choice(state, menu, choice);

	if (!state) {
		return 0;
	}
	state->invocations++;
	if (index < 0) {
		state->invalid++;
		return 0;
	}
	state->choices[index].count++;
	return 1;
}

static size_t append_text(char *output, size_t output_size, size_t used,
	const char *format, ...)
{
	va_list arguments;
	int written;

	if (used >= output_size) {
		return used;
	}
	va_start(arguments, format);
	written = vsnprintf(output + used, output_size - used, format, arguments);
	va_end(arguments);
	if (written < 0) {
		return used;
	}
	if ((size_t)written >= output_size - used) {
		return output_size;
	}
	return used + (size_t)written;
}

size_t tutorial_format_metrics(const tutorial_counter_state_t *state,
	uint64_t uptime_seconds, int json, char *output, size_t output_size)
{
	size_t used = 0;
	uint32_t index;

	if (!state || !output || !output_size) {
		return 0;
	}
	output[0] = '\0';
	if (json) {
		used = append_text(output, output_size, used,
			"{\"version\":\"1\",\"uptime_seconds\":%" PRIu64
			",\"invocations\":%" PRIu64 ",\"invalid\":%" PRIu64 ",\"choices\":[",
			uptime_seconds, state->invocations, state->invalid);
		for (index = 0; index < state->choice_count; index++) {
			used = append_text(output, output_size, used,
				"%s{\"menu\":\"%s\",\"choice\":\"%s\",\"count\":%" PRIu64 "}",
				index ? "," : "", state->choices[index].menu,
				state->choices[index].choice, state->choices[index].count);
		}
		used = append_text(output, output_size, used, "]}\n");
	} else {
		used = append_text(output, output_size, used,
			"version=1\nuptime_seconds=%" PRIu64 "\ninvocations=%" PRIu64
			"\ninvalid=%" PRIu64 "\n",
			uptime_seconds, state->invocations, state->invalid);
		for (index = 0; index < state->choice_count; index++) {
			used = append_text(output, output_size, used,
				"choice.%s.%s=%" PRIu64 "\n", state->choices[index].menu,
				state->choices[index].choice, state->choices[index].count);
		}
	}
	if (used >= output_size) {
		output[output_size - 1] = '\0';
		return output_size - 1;
	}
	return used;
}

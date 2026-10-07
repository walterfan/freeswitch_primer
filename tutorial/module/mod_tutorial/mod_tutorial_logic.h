#ifndef MOD_TUTORIAL_LOGIC_H
#define MOD_TUTORIAL_LOGIC_H

#include <stddef.h>
#include <stdint.h>

#define TUTORIAL_MAX_MENUS 8
#define TUTORIAL_MAX_CHOICES 32
#define TUTORIAL_MAX_MENU_LENGTH 32

typedef struct {
	char menu[TUTORIAL_MAX_MENU_LENGTH + 1];
	char choice[2];
	uint64_t count;
} tutorial_choice_t;

typedef struct {
	uint64_t invocations;
	uint64_t invalid;
	tutorial_choice_t choices[TUTORIAL_MAX_CHOICES];
	uint32_t choice_count;
} tutorial_counter_state_t;

int tutorial_valid_menu(const char *menu);
int tutorial_valid_choice(const char *choice);
int tutorial_find_choice(const tutorial_counter_state_t *state,
	const char *menu, const char *choice);
int tutorial_record_choice(tutorial_counter_state_t *state,
	const char *menu, const char *choice);
size_t tutorial_format_metrics(const tutorial_counter_state_t *state,
	uint64_t uptime_seconds, int json, char *output, size_t output_size);

#endif

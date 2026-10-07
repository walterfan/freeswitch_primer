#include "../mod_tutorial_logic.h"

#include <assert.h>
#include <pthread.h>
#include <string.h>

typedef struct {
	tutorial_counter_state_t *state;
	pthread_mutex_t *mutex;
} worker_context_t;

static void add_choice(tutorial_counter_state_t *state,
	const char *menu, const char *choice)
{
	tutorial_choice_t *entry = &state->choices[state->choice_count++];
	strncpy(entry->menu, menu, sizeof(entry->menu) - 1);
	strncpy(entry->choice, choice, sizeof(entry->choice) - 1);
}

static void *record_worker(void *opaque)
{
	worker_context_t *context = opaque;
	int index;

	for (index = 0; index < 1000; index++) {
		pthread_mutex_lock(context->mutex);
		assert(tutorial_record_choice(context->state, "main", "1"));
		pthread_mutex_unlock(context->mutex);
	}
	return NULL;
}

static void test_validation(void)
{
	char long_menu[TUTORIAL_MAX_MENU_LENGTH + 2];

	memset(long_menu, 'a', sizeof(long_menu) - 1);
	long_menu[sizeof(long_menu) - 1] = '\0';
	assert(tutorial_valid_menu("main"));
	assert(tutorial_valid_menu("sub_menu-1"));
	assert(!tutorial_valid_menu(""));
	assert(!tutorial_valid_menu("menu with spaces"));
	assert(!tutorial_valid_menu(long_menu));
	assert(tutorial_valid_choice("0"));
	assert(tutorial_valid_choice("*"));
	assert(tutorial_valid_choice("#"));
	assert(!tutorial_valid_choice(""));
	assert(!tutorial_valid_choice("12"));
	assert(!tutorial_valid_choice("a"));
}

static void test_recording_and_formatting(void)
{
	tutorial_counter_state_t state;
	tutorial_counter_state_t before;
	char text[4096];
	char json[4096];

	memset(&state, 0, sizeof(state));
	add_choice(&state, "main", "1");
	add_choice(&state, "main", "2");
	add_choice(&state, "submenu", "1");
	assert(tutorial_record_choice(&state, "main", "1"));
	assert(!tutorial_record_choice(&state, "unknown", "1"));
	assert(!tutorial_record_choice(&state, "main", "12"));
	assert(state.invocations == 3);
	assert(state.invalid == 2);
	assert(state.choices[0].count == 1);
	assert(state.choices[1].count == 0);

	before = state;
	assert(tutorial_format_metrics(&state, 12, 0, text, sizeof(text)) > 0);
	assert(strstr(text, "version=1\n"));
	assert(strstr(text, "uptime_seconds=12\n"));
	assert(strstr(text, "invocations=3\n"));
	assert(strstr(text, "invalid=2\n"));
	assert(strstr(text, "choice.main.1=1\n"));
	assert(tutorial_format_metrics(&state, 12, 1, json, sizeof(json)) > 0);
	assert(strstr(json, "\"version\":\"1\""));
	assert(strstr(json, "\"uptime_seconds\":12"));
	assert(strstr(json, "\"invocations\":3"));
	assert(strstr(json, "\"invalid\":2"));
	assert(strstr(json, "\"menu\":\"main\",\"choice\":\"1\",\"count\":1"));
	assert(memcmp(&state, &before, sizeof(state)) == 0);
}

static void test_concurrent_recording(void)
{
	enum { worker_count = 4 };
	tutorial_counter_state_t state;
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	worker_context_t context;
	pthread_t workers[worker_count];
	int index;

	memset(&state, 0, sizeof(state));
	add_choice(&state, "main", "1");
	context.state = &state;
	context.mutex = &mutex;
	for (index = 0; index < worker_count; index++) {
		assert(!pthread_create(&workers[index], NULL, record_worker, &context));
	}
	for (index = 0; index < worker_count; index++) {
		assert(!pthread_join(workers[index], NULL));
	}
	assert(state.invocations == worker_count * 1000);
	assert(state.invalid == 0);
	assert(state.choices[0].count == worker_count * 1000);
	pthread_mutex_destroy(&mutex);
}

int main(void)
{
	test_validation();
	test_recording_and_formatting();
	test_concurrent_recording();
	return 0;
}

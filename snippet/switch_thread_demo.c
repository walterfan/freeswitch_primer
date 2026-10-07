#include <switch.h>
#include <switch_apr.h>

typedef struct demo_task {
	void (*run)(void *arg);
	void *arg;
} demo_task_t;

static void demo_task_run(void *arg)
{
	const char *name = (const char *)arg;

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "running task: %s\n", name);
}

static void *SWITCH_THREAD_FUNC worker(switch_thread_t *thread, void *data)
{
	switch_queue_t *queue = (switch_queue_t *)data;

	(void)thread;

	for (;;) {
		void *pop = NULL;
		switch_status_t status = switch_queue_pop_timeout(queue, &pop, 1000000);

		if (switch_status_is_timeup(status)) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "queue empty; retrying\n");
			continue;
		}

		if (status != SWITCH_STATUS_SUCCESS || !pop) {
			break;
		}

		{
			demo_task_t *task = (demo_task_t *)pop;
			if (task->run) {
				task->run(task->arg);
			}
		}
	}

	return NULL;
}

int main(void)
{
	const char *err = NULL;
	switch_memory_pool_t *pool = NULL;
	switch_threadattr_t *attr = NULL;
	switch_thread_t *thread = NULL;
	switch_queue_t *queue = NULL;
	switch_status_t status = SWITCH_STATUS_GENERR;
	switch_status_t join_status = SWITCH_STATUS_GENERR;
	demo_task_t tasks[] = {
		{ demo_task_run, (void *)"one" },
		{ demo_task_run, (void *)"two" },
		{ demo_task_run, (void *)"three" },
		{ demo_task_run, (void *)"four" },
		{ demo_task_run, (void *)"five" }
	};

	switch_core_set_globals();
	if (switch_core_init(SCF_MINIMAL, SWITCH_TRUE, &err) != SWITCH_STATUS_SUCCESS) {
		fprintf(stderr, "Cannot initialize FreeSWITCH core: %s\n", err ? err : "unknown error");
		return 1;
	}

	if (switch_core_new_memory_pool(&pool) != SWITCH_STATUS_SUCCESS ||
		switch_threadattr_create(&attr, pool) != SWITCH_STATUS_SUCCESS ||
		switch_queue_create(&queue, 16, pool) != SWITCH_STATUS_SUCCESS) {
		goto done;
	}

	if (switch_thread_create(&thread, attr, worker, queue, pool) == SWITCH_STATUS_SUCCESS) {
		status = SWITCH_STATUS_SUCCESS;

		for (size_t i = 0; i < sizeof(tasks) / sizeof(tasks[0]); i++) {
			if (switch_queue_push(queue, &tasks[i]) != SWITCH_STATUS_SUCCESS) {
				status = SWITCH_STATUS_GENERR;
				break;
			}
		}

		/* Give the worker time to demonstrate the one-second empty wait. */
		switch_sleep(2000000);
		if (switch_queue_push(queue, NULL) != SWITCH_STATUS_SUCCESS) {
			status = SWITCH_STATUS_GENERR;
		}

		if (switch_thread_join(&join_status, thread) != SWITCH_STATUS_SUCCESS ||
			join_status != SWITCH_STATUS_SUCCESS) {
			status = SWITCH_STATUS_GENERR;
		}
	}

done:
	if (pool) {
		switch_core_destroy_memory_pool(&pool);
	}
	switch_core_destroy();

	return status == SWITCH_STATUS_SUCCESS ? 0 : 1;
}

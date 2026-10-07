#include <switch.h>

typedef struct demo_object {
	switch_memory_pool_t *pool;
	char *name;
	char *temp_path;
	switch_file_t *file;
	switch_threadattr_t *thread_attr;
	switch_thread_t *thread;
	switch_status_t worker_status;
} demo_object_t;

static void *SWITCH_THREAD_FUNC worker(switch_thread_t *thread, void *data)
{
	demo_object_t *object = (demo_object_t *)data;
	switch_status_t status = SWITCH_STATUS_GENERR;

	if (object && switch_core_memory_pool_get_data(object->pool, "demo.object") == object &&
		object->file && switch_file_printf(object->file, "hello from %s\n", object->name) >= 0) {
		status = SWITCH_STATUS_SUCCESS;
	}

	if (object) {
		object->worker_status = status;
	}
	switch_thread_exit(thread, status);
	return NULL;
}
/* Create a demo object and initialize its resources. */
static switch_status_t demo_object_create(demo_object_t **object, switch_memory_pool_t *pool)
{
	*object = switch_core_alloc(pool, sizeof(**object));
	if (!*object || !SWITCH_GLOBAL_dirs.temp_dir) {
		return SWITCH_STATUS_MEMERR;
	}

	(*object)->pool = pool;
	(*object)->worker_status = SWITCH_STATUS_GENERR;
    // Initialize the object's fields, switch_core_strdup is to duplicate the string into the memory pool
	(*object)->name = switch_core_strdup(pool, "pool-owned-object");
	(*object)->temp_path = switch_core_sprintf(pool, "%s%sfs-memory-pool-demo.XXXXXX",
										   SWITCH_GLOBAL_dirs.temp_dir, SWITCH_PATH_SEPARATOR);
	if (!(*object)->name || !(*object)->temp_path) {
		return SWITCH_STATUS_MEMERR;
	}

	/* The file cleanup is registered by switch_file_mktemp with this pool. */
	if (switch_file_mktemp(&(*object)->file, (*object)->temp_path, 0, pool) != SWITCH_STATUS_SUCCESS ||
		switch_threadattr_create(&(*object)->thread_attr, pool) != SWITCH_STATUS_SUCCESS) {
		return SWITCH_STATUS_GENERR;
	}

	/* Pool data is useful when a callback needs to recover its owning object. */
	switch_core_memory_pool_set_data(pool, "demo.object", *object);
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t demo_object_start(demo_object_t *object)
{
	return switch_thread_create(&object->thread, object->thread_attr, worker, object, object->pool);
}

int main(void)
{
	const char *err = NULL;
	switch_memory_pool_t *pool = NULL;
	demo_object_t *object = NULL;
	char temp_path[PATH_MAX] = {0};
	switch_status_t thread_status = SWITCH_STATUS_GENERR;
	switch_status_t status = SWITCH_STATUS_GENERR;

	switch_core_set_globals();
	if (switch_core_init(SCF_MINIMAL, SWITCH_TRUE, &err) != SWITCH_STATUS_SUCCESS) {
		fprintf(stderr, "Cannot initialize FreeSWITCH core: %s\n", err ? err : "unknown error");
		return 1;
	}

	/* allocate memory from pool -> initialize resource -> register cleanup */
	if (switch_core_new_memory_pool(&pool) != SWITCH_STATUS_SUCCESS ||
		demo_object_create(&object, pool) != SWITCH_STATUS_SUCCESS) { // create demo object
		goto done;
	}
	switch_copy_string(temp_path, object->temp_path, sizeof(temp_path));
	if (demo_object_start(object) != SWITCH_STATUS_SUCCESS) { // start worker thread
		goto done;
	}

	/* Join before destroying the pool: the worker still uses pool-owned data. */
	if (switch_thread_join(&thread_status, object->thread) != SWITCH_STATUS_SUCCESS) {
		fprintf(stderr, "Cannot join worker thread\n");
		switch_core_destroy();
		return 1;
	}
	object->thread = NULL;
	if (thread_status != SWITCH_STATUS_SUCCESS || object->worker_status != SWITCH_STATUS_SUCCESS) {
		goto done;
	}

	status = SWITCH_STATUS_SUCCESS;

	done:
	if (pool) {
		if (switch_core_destroy_memory_pool(&pool) != SWITCH_STATUS_SUCCESS) {
			status = SWITCH_STATUS_GENERR;
		} else if (temp_path[0]) {
			/* Pool destruction is asynchronous in the default FreeSWITCH build. */
			for (int attempt = 0; attempt < 30 && switch_file_exists(temp_path, NULL) == SWITCH_STATUS_SUCCESS; attempt++) {
				switch_sleep(100000);
			}
			if (switch_file_exists(temp_path, NULL) == SWITCH_STATUS_SUCCESS) {
				fprintf(stderr, "Pool cleanup failed: temporary file still exists: %s\n", temp_path);
				status = SWITCH_STATUS_GENERR;
			} else {
				fprintf(stdout, "Pool cleanup verified: temporary file was removed\n");
			}
		}
	}
	switch_core_destroy();

	return status == SWITCH_STATUS_SUCCESS ? 0 : 1;
}

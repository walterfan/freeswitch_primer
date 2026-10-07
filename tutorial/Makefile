SITE_DIR ?= site
SITE_BUILD_DIR ?= $(SITE_DIR)/build
SITE_BINARY ?= $(SITE_BUILD_DIR)/freeswitch-tutorial-site
SITE_CONFIG ?= $(SITE_DIR)/config/config.yaml
SITE_PID_FILE ?= $(SITE_DIR)/.tutorial-site.pid
SITE_LOG_FILE ?= $(SITE_DIR)/.tutorial-site.log

.DEFAULT_GOAL := help

.PHONY: help configure build site-build site-start site-stop start stop test validate clean

help:
	@printf '%s\n' \
		'FreeSWITCH 30-day tutorial commands:' \
		'  make build       Build tutorial checks and the teaching site' \
		'  make start       Start the teaching site (run make build first)' \
		'  make stop        Stop the teaching site safely' \
		'  make site-build  Build only the teaching site' \
		'  make site-start  Start only the teaching site' \
		'  make site-stop   Stop only the teaching site' \
		'  make test        Run tutorial contract tests' \
		'  make validate    Validate all lesson content' \
		'  make clean       Remove the tutorial CMake build directory'

configure:
	cmake -S . -B build

build: configure
	cmake --build build
	$(MAKE) site-build

site-build:
	bash $(SITE_DIR)/build.sh

site-start:
	@pid_file="$(SITE_PID_FILE)"; \
	if [ ! -x "$(SITE_BINARY)" ]; then \
		printf 'tutorial site is not built; run make build first\n' >&2; \
		exit 1; \
	fi; \
	if [ -f "$$pid_file" ]; then \
		pid="$$(<"$$pid_file")"; \
		command="$$(ps -p "$$pid" -o command= 2>/dev/null || true)"; \
		case "$$command" in \
			*"freeswitch-tutorial-site"*) \
				printf 'tutorial site already running (pid %s)\n' "$$pid"; \
				exit 0 ;; \
			*) rm -f "$$pid_file" ;; \
		esac; \
	fi; \
	nohup "$(SITE_BINARY)" --config "$(SITE_CONFIG)" \
		>"$(SITE_LOG_FILE)" 2>&1 & \
	echo $$! >"$$pid_file"; \
	sleep 1; \
	pid="$$(<"$$pid_file")"; \
	if ! kill -0 "$$pid" 2>/dev/null; then \
		printf 'tutorial site failed to start; inspect %s\n' "$(SITE_LOG_FILE)" >&2; \
		rm -f "$$pid_file"; \
		exit 1; \
	fi; \
	printf 'tutorial site started (pid %s)\n' "$$pid"

site-stop:
	@pid_file="$(SITE_PID_FILE)"; \
	if [ ! -f "$$pid_file" ]; then \
		printf 'tutorial site is not running\n'; \
		exit 0; \
	fi; \
	pid="$$(<"$$pid_file")"; \
	command="$$(ps -p "$$pid" -o command= 2>/dev/null || true)"; \
	case "$$command" in \
		*"freeswitch-tutorial-site"*) \
			kill "$$pid"; \
			for attempt in $$(seq 1 30); do \
				kill -0 "$$pid" 2>/dev/null || break; \
				sleep 0.1; \
			done; \
			if kill -0 "$$pid" 2>/dev/null; then \
				printf 'tutorial site did not stop (pid %s)\n' "$$pid" >&2; \
				exit 1; \
			fi; \
			printf 'tutorial site stopped (pid %s)\n' "$$pid" ;; \
		*) printf 'removing stale tutorial site pid file\n' ;; \
	esac; \
	rm -f "$$pid_file"

start: site-start

stop: site-stop

test: configure
	ctest --test-dir build --output-on-failure

validate:
	python3 scripts/validate_content.py content/manifest.json --tutorial-root .

clean:
	cmake -E remove_directory build


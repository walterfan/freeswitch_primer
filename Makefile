DOC_DIR ?= man
TUTORIAL_DIR ?= tutorial
MANUAL_PORT ?= 7008
MANUAL_PID_FILE ?= $(DOC_DIR)/_build/manual.pid
MANUAL_LOG_FILE ?= $(DOC_DIR)/_build/manual.log
PYTHON ?= python3

.DEFAULT_GOAL := help

.PHONY: help all deps build-manual build-tutorial start-manual stop-manual start-tutorial stop-tutorial clean

help:
	@printf '%s\n' \
		'FreeSWITCH Primer:' \
		'  make deps       Install Python (Poetry in man/)' \
		'  make build-manual Build Sphinx PKB HTML' \
		'  make build-tutorial Build tutorial checks ' \
		'  make start-manual   Build and serve the manual at http://127.0.0.1:7008/' \
		'  make stop-manual    Stop the manual server' \
		'  make start-tutorial Start the tutorial site at http://127.0.0.1:7009/' \
		'  make stop-tutorial  Stop the tutorial site' \
		'  make all        Build manual, tutorial' \
		'  make clean      Remove manual and tutorial build directories'

all: build-manual build-tutorial

deps:
	$(MAKE) -C $(DOC_DIR) deps

build-manual:
	$(MAKE) -C $(DOC_DIR) html-all

build-tutorial:
	$(MAKE) -C $(TUTORIAL_DIR) build

start-manual: build-manual
	@case "$(MANUAL_PORT)" in ''|*[!0-9]*) echo "ERROR: MANUAL_PORT must be numeric." >&2; exit 1;; esac
	@mkdir -p "$(dir $(MANUAL_PID_FILE))"
	@if [ -f "$(MANUAL_PID_FILE)" ]; then \
		pid="$$(cat "$(MANUAL_PID_FILE)")"; \
		command="$$(ps -p "$$pid" -o command= 2>/dev/null || true)"; \
		case "$$command" in \
			*"http.server "*"$(DOC_DIR)/_build/site"*|*"http.server "*"$(DOC_DIR)/_build/html"*) echo "manual already running (pid $$pid)"; exit 0 ;; \
			*) rm -f "$(MANUAL_PID_FILE)" ;; \
		esac; \
	fi
	@nohup "$(PYTHON)" -m http.server "$(MANUAL_PORT)" --bind 127.0.0.1 --directory "$(DOC_DIR)/_build/site" \
		>"$(MANUAL_LOG_FILE)" 2>&1 </dev/null & echo $$! >"$(MANUAL_PID_FILE)"
	@sleep 1
	@pid="$$(cat "$(MANUAL_PID_FILE)")"; \
	if ! kill -0 "$$pid" 2>/dev/null; then \
		echo "manual server failed to start; inspect $(MANUAL_LOG_FILE)" >&2; \
		rm -f "$(MANUAL_PID_FILE)"; exit 1; \
	fi; \
	echo "manual started: http://127.0.0.1:$(MANUAL_PORT)/ (pid $$pid)"

stop-manual:
	@if [ ! -f "$(MANUAL_PID_FILE)" ]; then echo "manual is not running"; exit 0; fi
	@pid="$$(cat "$(MANUAL_PID_FILE)")"; \
	case "$$pid" in ''|*[!0-9]*) echo "invalid manual PID file; removing it" >&2; rm -f "$(MANUAL_PID_FILE)"; exit 1;; esac; \
	command="$$(ps -p "$$pid" -o command= 2>/dev/null || true)"; \
	case "$$command" in \
		*"http.server "*"$(DOC_DIR)/_build/site"*|*"http.server "*"$(DOC_DIR)/_build/html"*) \
			kill "$$pid"; \
			for attempt in $$(seq 1 30); do kill -0 "$$pid" 2>/dev/null || break; sleep 0.1; done; \
			if kill -0 "$$pid" 2>/dev/null; then echo "manual server did not stop (pid $$pid)" >&2; exit 1; fi; \
			echo "manual stopped (pid $$pid)" ;; \
		*) echo "removing stale manual PID file" ;; \
	esac; \
	rm -f "$(MANUAL_PID_FILE)"

start-tutorial:
	$(MAKE) -C $(TUTORIAL_DIR) start

stop-tutorial:
	$(MAKE) -C $(TUTORIAL_DIR) stop

clean:
	$(MAKE) -C $(DOC_DIR) clean
	$(MAKE) -C $(TUTORIAL_DIR) clean

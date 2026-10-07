CMAKE ?= cmake
BUILD_DIR ?= build
FREESWITCH_PC_FILE ?= $(HOME)/fs/lib/pkgconfig/freeswitch.pc

CMAKE_ARGS ?= -DFREESWITCH_PC_FILE=$(FREESWITCH_PC_FILE)

.DEFAULT_GOAL := help

.PHONY: help all configure build run run-thread run-memory run-bridge clean

help:
	@echo "Available targets:"
	@echo "  make build       Configure and build both demos"
	@echo "  make run         Build and execute both demos"
	@echo "  make run-thread  Build and execute switch_thread_demo"
	@echo "  make run-memory  Build and execute switch_memory_pool_demo"
	@echo "  make run-bridge  Build and execute switch_audio_bridge_demo"
	@echo "  make clean       Remove CMake build artifacts"

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) $(CMAKE_ARGS)

build: configure
	$(CMAKE) --build $(BUILD_DIR)

run: run-thread run-memory run-bridge

run-bridge: build
	$(BUILD_DIR)/switch_audio_bridge_demo

run-thread: build
	$(BUILD_DIR)/switch_thread_demo

run-memory: build
	$(BUILD_DIR)/switch_memory_pool_demo

clean:
	$(CMAKE) --build $(BUILD_DIR) --target clean

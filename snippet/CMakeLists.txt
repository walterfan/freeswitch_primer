cmake_minimum_required(VERSION 3.15)
project(switch_thread_demo LANGUAGES C)

set(FREESWITCH_PC_FILE "" CACHE FILEPATH "Path to the installed freeswitch.pc")
if(FREESWITCH_PC_FILE)
    get_filename_component(FREESWITCH_PC_DIR "${FREESWITCH_PC_FILE}" DIRECTORY)
    set(ENV{PKG_CONFIG_PATH} "${FREESWITCH_PC_DIR}:$ENV{PKG_CONFIG_PATH}")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FREESWITCH REQUIRED IMPORTED_TARGET freeswitch)

add_executable(switch_thread_demo switch_thread_demo.c)
target_link_libraries(switch_thread_demo PRIVATE PkgConfig::FREESWITCH)
target_compile_options(switch_thread_demo PRIVATE ${FREESWITCH_CFLAGS_OTHER})

add_executable(switch_memory_pool_demo switch_memory_pool_demo.c)
target_link_libraries(switch_memory_pool_demo PRIVATE PkgConfig::FREESWITCH)
target_compile_options(switch_memory_pool_demo PRIVATE ${FREESWITCH_CFLAGS_OTHER})

add_executable(switch_audio_bridge_demo switch_audio_bridge_demo.c)
target_link_libraries(switch_audio_bridge_demo PRIVATE PkgConfig::FREESWITCH)
target_compile_options(switch_audio_bridge_demo PRIVATE ${FREESWITCH_CFLAGS_OTHER})
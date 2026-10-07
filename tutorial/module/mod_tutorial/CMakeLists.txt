cmake_minimum_required(VERSION 3.15)
project(mod_tutorial LANGUAGES C)

set(FREESWITCH_PC_FILE "" CACHE FILEPATH "Path to the installed freeswitch.pc")
if(FREESWITCH_PC_FILE)
    get_filename_component(FREESWITCH_PC_DIR "${FREESWITCH_PC_FILE}" DIRECTORY)
    set(ENV{PKG_CONFIG_PATH} "${FREESWITCH_PC_DIR}:$ENV{PKG_CONFIG_PATH}")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(FREESWITCH REQUIRED IMPORTED_TARGET freeswitch)

add_library(mod_tutorial MODULE mod_tutorial.c mod_tutorial_logic.c)
target_link_libraries(mod_tutorial PRIVATE PkgConfig::FREESWITCH)
target_compile_options(mod_tutorial PRIVATE ${FREESWITCH_CFLAGS_OTHER})
set_target_properties(mod_tutorial PROPERTIES PREFIX "" OUTPUT_NAME "mod_tutorial")

option(BUILD_TESTS "Build standalone module contract tests" ON)
if(BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)
    add_executable(mod-tutorial-unit-tests
        test/test_mod_tutorial.c
        mod_tutorial_logic.c)
    target_link_libraries(mod-tutorial-unit-tests PRIVATE Threads::Threads)
    add_test(NAME mod-tutorial-unit-tests COMMAND mod-tutorial-unit-tests)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME mod-tutorial-contract-tests
             COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test/test_mod_tutorial.py")
    add_test(NAME mod-tutorial-integration
             COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/module_integration.sh")
    set_tests_properties(mod-tutorial-integration PROPERTIES SKIP_RETURN_CODE 77)
endif()

install(TARGETS mod_tutorial LIBRARY DESTINATION lib/freeswitch/mod)
install(FILES mod_tutorial.conf.xml DESTINATION etc/freeswitch/autoload_configs)
install(FILES tutorial-ivr.xml DESTINATION share/freeswitch-tutorial)

cmake_minimum_required(VERSION 3.15)
project(freeswitch_tutorial LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
enable_testing()

set(TUTORIAL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(TUTORIAL_MANIFEST "${TUTORIAL_ROOT}/content/manifest.json")
set(TUTORIAL_VALIDATOR "${TUTORIAL_ROOT}/scripts/validate_content.py")

add_custom_target(tutorial-content
    COMMAND "${Python3_EXECUTABLE}" "${TUTORIAL_VALIDATOR}"
            "${TUTORIAL_MANIFEST}" --tutorial-root "${TUTORIAL_ROOT}"
    WORKING_DIRECTORY "${TUTORIAL_ROOT}"
    COMMENT "Validate the complete 30-day tutorial content")

add_test(
    NAME tutorial-content-validator-fixtures
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
            -s "${TUTORIAL_ROOT}/tests" -p "test_*.py")

add_custom_target(tutorial-tests
    COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure
    COMMENT "Run tutorial tests")

option(TUTORIAL_BUILD_SITE "Build the C++ teaching service from the tutorial root" OFF)
if(TUTORIAL_BUILD_SITE)
    add_subdirectory(site)
else()
    add_custom_target(tutorial-site
        COMMAND "${CMAKE_COMMAND}" -E echo "Configure tutorial/site with Conan to build the teaching service")
endif()

option(TUTORIAL_BUILD_MODULE "Build mod_tutorial from the tutorial root" OFF)
if(TUTORIAL_BUILD_MODULE)
    add_subdirectory(module/mod_tutorial)
else()
    add_custom_target(tutorial-module
        COMMAND "${CMAKE_COMMAND}" -E echo
                "Configure tutorial/module/mod_tutorial with TUTORIAL_BUILD_MODULE=ON and FREESWITCH_PC_FILE")
endif()

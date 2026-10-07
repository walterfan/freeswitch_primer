cmake_minimum_required(VERSION 3.15)
project(freeswitch_tutorial_site LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TUTORIAL_CROW_INCLUDE "" CACHE PATH "Path containing crow.h for read-only/offline builds")
set(TUTORIAL_ASIO_INCLUDE "" CACHE PATH "Path containing asio.hpp for read-only/offline builds")
set(TUTORIAL_YAML_INCLUDE "" CACHE PATH "Path containing yaml-cpp/yaml.h for read-only/offline builds")
set(TUTORIAL_YAML_LIBRARY "" CACHE FILEPATH "Path to a yaml-cpp library for read-only/offline builds")
option(TUTORIAL_ENABLE_ESL "Link the teaching service with the repository ESL client" OFF)
set(TUTORIAL_ESL_INCLUDE "" CACHE PATH "Path containing the repository ESL headers")
set(TUTORIAL_ESL_LIBRARY "" CACHE FILEPATH "Path to the repository ESL client library")

if(TUTORIAL_ENABLE_ESL)
    if(NOT IS_DIRECTORY "${TUTORIAL_ESL_INCLUDE}" OR
       NOT EXISTS "${TUTORIAL_ESL_INCLUDE}/esl.h")
        message(FATAL_ERROR
            "ESL headers were not found. Set TUTORIAL_ESL_INCLUDE to libs/esl/src/include.")
    endif()
    if(NOT EXISTS "${TUTORIAL_ESL_LIBRARY}")
        message(FATAL_ERROR
            "ESL library was not found. Build libs/esl and set TUTORIAL_ESL_LIBRARY.")
    endif()
    add_library(tutorial_esl UNKNOWN IMPORTED)
    set_target_properties(tutorial_esl PROPERTIES
        IMPORTED_LOCATION "${TUTORIAL_ESL_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${TUTORIAL_ESL_INCLUDE}")
endif()

find_package(Crow CONFIG QUIET)
find_package(yaml-cpp CONFIG QUIET)
find_package(OpenSSL REQUIRED)

if(TARGET Crow::Crow)
    set(TUTORIAL_CROW_TARGET Crow::Crow)
elseif(TUTORIAL_CROW_INCLUDE AND TUTORIAL_ASIO_INCLUDE)
    add_library(tutorial_crow_headers INTERFACE)
    target_include_directories(tutorial_crow_headers INTERFACE
        "${TUTORIAL_CROW_INCLUDE}"
        "${TUTORIAL_ASIO_INCLUDE}")
    set(TUTORIAL_CROW_TARGET tutorial_crow_headers)
else()
    message(FATAL_ERROR
        "Crow was not found. Use Conan or set TUTORIAL_CROW_INCLUDE and TUTORIAL_ASIO_INCLUDE.")
endif()

if(TARGET yaml-cpp::yaml-cpp)
    set(TUTORIAL_YAML_TARGET yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
    set(TUTORIAL_YAML_TARGET yaml-cpp)
elseif(TUTORIAL_YAML_INCLUDE AND TUTORIAL_YAML_LIBRARY)
    add_library(tutorial_yaml_cpp UNKNOWN IMPORTED)
    set_target_properties(tutorial_yaml_cpp PROPERTIES
        IMPORTED_LOCATION "${TUTORIAL_YAML_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${TUTORIAL_YAML_INCLUDE}")
    set(TUTORIAL_YAML_TARGET tutorial_yaml_cpp)
else()
    message(FATAL_ERROR
        "yaml-cpp was not found. Use Conan or set TUTORIAL_YAML_INCLUDE and TUTORIAL_YAML_LIBRARY.")
endif()

add_library(tutorial_site_core
    src/check_registry.cpp
    src/config.cpp
    src/health.cpp
    src/logging.cpp
    src/redaction.cpp
    src/esl_observer.cpp
    src/metrics.cpp
)
if(TUTORIAL_ENABLE_ESL)
    target_sources(tutorial_site_core PRIVATE src/esl_client.cpp)
endif()
target_include_directories(tutorial_site_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(tutorial_site_core PUBLIC ${TUTORIAL_YAML_TARGET})
if(TUTORIAL_ENABLE_ESL)
    target_link_libraries(tutorial_site_core PUBLIC tutorial_esl)
    target_compile_definitions(tutorial_site_core PUBLIC TUTORIAL_ENABLE_ESL)
endif()

add_executable(freeswitch-tutorial-site
    src/http_server.cpp
    src/main.cpp
)
target_include_directories(freeswitch-tutorial-site PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(freeswitch-tutorial-site PRIVATE tutorial_site_core ${TUTORIAL_CROW_TARGET})
target_compile_definitions(freeswitch-tutorial-site PRIVATE CROW_ENABLE_SSL)
target_link_libraries(freeswitch-tutorial-site PRIVATE OpenSSL::SSL OpenSSL::Crypto)

file(GLOB TUTORIAL_WEB_ASSETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/*")
file(GLOB TUTORIAL_CONFIG_ASSETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/config/*")
add_custom_target(tutorial-site-assets ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/web"
            "${CMAKE_CURRENT_BINARY_DIR}/web"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/config"
            "${CMAKE_CURRENT_BINARY_DIR}/config"
    DEPENDS ${TUTORIAL_WEB_ASSETS} ${TUTORIAL_CONFIG_ASSETS})
add_dependencies(freeswitch-tutorial-site tutorial-site-assets)

option(BUILD_TESTS "Build tutorial site unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_executable(tutorial-site-unit-tests test/unit_tests.cpp)
    target_link_libraries(tutorial-site-unit-tests PRIVATE tutorial_site_core)
    add_test(NAME tutorial-site-unit-tests COMMAND tutorial-site-unit-tests)
    add_test(
        NAME tutorial-site-http-smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test/http_smoke.py"
                "$<TARGET_FILE:freeswitch-tutorial-site>")
    set_tests_properties(tutorial-site-http-smoke PROPERTIES TIMEOUT 20)
    find_program(NODE_EXECUTABLE node)
    if(NODE_EXECUTABLE)
        add_test(NAME tutorial-site-web-tests
                 COMMAND "${NODE_EXECUTABLE}" --test "${CMAKE_CURRENT_SOURCE_DIR}/test/web_tests.mjs")
    endif()
endif()

install(TARGETS freeswitch-tutorial-site RUNTIME DESTINATION bin)
install(DIRECTORY web/ DESTINATION bin/web)
install(FILES config/config.yaml DESTINATION bin/config)

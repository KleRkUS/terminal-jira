# Copyright (C) 2026 the terminal-jira authors
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)
project(terminal-jira LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Chosen by the user or by CI. The machine CMake happens to be running on is not
# consulted: a Linux runner can still be asked for another platform.
set(TERMINAL_JIRA_PLATFORM "linux" CACHE STRING
    "Platform module to compile: linux (default), mac or windows")
set_property(CACHE TERMINAL_JIRA_PLATFORM PROPERTY STRINGS linux mac windows)

set(_terminal_jira_platforms linux mac windows)
if(NOT TERMINAL_JIRA_PLATFORM IN_LIST _terminal_jira_platforms)
  message(FATAL_ERROR
    "TERMINAL_JIRA_PLATFORM must be one of: linux, mac, windows "
    "(got '${TERMINAL_JIRA_PLATFORM}').")
endif()

set(TERMINAL_JIRA_PLATFORM_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/${TERMINAL_JIRA_PLATFORM}.cpp")
if(NOT EXISTS "${TERMINAL_JIRA_PLATFORM_SOURCE}")
  message(FATAL_ERROR
    "TERMINAL_JIRA_PLATFORM=${TERMINAL_JIRA_PLATFORM} requires "
    "src/platform/${TERMINAL_JIRA_PLATFORM}.cpp, and that file does not exist yet.")
endif()

message(STATUS "terminal-jira platform: ${TERMINAL_JIRA_PLATFORM}")

include(FetchContent)

FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG v5.0.0
  GIT_SHALLOW TRUE)

FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)

FetchContent_MakeAvailable(ftxui json)

find_package(CURL REQUIRED)

# The chosen platform module, plus the name of that choice, on every target that
# compiles it. Further per-platform target settings belong here too.
function(terminal_jira_platform target)
  target_sources(${target} PRIVATE "${TERMINAL_JIRA_PLATFORM_SOURCE}")
  target_compile_definitions(${target} PRIVATE "TERMINAL_JIRA_PLATFORM=\"${TERMINAL_JIRA_PLATFORM}\"")
endfunction()

add_executable(terminal-jira
  src/main.cpp
  src/app.cpp
  src/actions.cpp
  src/adf.cpp
  src/config.cpp
  src/translations.cpp
  src/jira_client.cpp
  src/strings/en.cpp
  src/ui.cpp
  src/windows/projects_window.cpp
  src/windows/project_window.cpp
  src/windows/ticket_window.cpp)
terminal_jira_platform(terminal-jira)

target_include_directories(terminal-jira PRIVATE src)
target_compile_options(terminal-jira PRIVATE -Wall -Wextra)
target_link_libraries(terminal-jira PRIVATE
  ftxui::screen ftxui::dom ftxui::component
  nlohmann_json::nlohmann_json
  CURL::libcurl)

option(TERMINAL_JIRA_TESTS "Build the unit tests" ON)
if(TERMINAL_JIRA_TESTS)
  enable_testing()

  add_executable(adf_test tests/adf_test.cpp src/adf.cpp)
  target_include_directories(adf_test PRIVATE src)
  target_compile_options(adf_test PRIVATE -Wall -Wextra)
  target_link_libraries(adf_test PRIVATE nlohmann_json::nlohmann_json)
  add_test(NAME adf COMMAND adf_test)

  add_executable(translations_test tests/translations_test.cpp src/translations.cpp src/strings/en.cpp
                 src/config.cpp)
  terminal_jira_platform(translations_test)
  target_include_directories(translations_test PRIVATE src)
  target_compile_options(translations_test PRIVATE -Wall -Wextra)
  target_link_libraries(translations_test PRIVATE nlohmann_json::nlohmann_json)
  add_test(NAME translations COMMAND translations_test)

  # End to end against tests/fake_jira.py. Needs python3 and script(1), so it is
  # registered only when both are present.
  find_package(Python3 COMPONENTS Interpreter)
  find_program(SCRIPT_EXECUTABLE script)
  if(Python3_Interpreter_FOUND AND SCRIPT_EXECUTABLE)
    add_test(NAME smoke
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.sh $<TARGET_FILE:terminal-jira>)
    set_tests_properties(smoke PROPERTIES TIMEOUT 300)
  endif()
endif()

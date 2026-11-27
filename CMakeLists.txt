# Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(Zaurus)

target_sources(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/app_clock_logo.c
)

# Revision of this application, shown next to Zephyr's
include(git)
git_describe(${CMAKE_CURRENT_SOURCE_DIR} APP_CLOCK_REVISION)
if(NOT APP_CLOCK_REVISION)
    set(APP_CLOCK_REVISION "unknown")
endif()
target_compile_definitions(app PRIVATE APP_CLOCK_REVISION="${APP_CLOCK_REVISION}")

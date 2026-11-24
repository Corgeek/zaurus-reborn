# Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_SVC_BEACON AND CONFIG_DRV_SHELL)
    target_sources(app PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/app_beacon_view.c
    )
endif()

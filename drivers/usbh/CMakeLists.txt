# Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
# SPDX-License-Identifier: Apache-2.0

target_sources_ifdef(CONFIG_DRV_USBH app PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/drv_usbh_common.c
)

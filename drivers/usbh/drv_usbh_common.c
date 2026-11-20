/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief USB host bring-up
 *
 * Initializes and enables the USB host stack on the zephyr_uhc0
 * controller at boot, so that the class drivers (hub, Bluetooth HCI)
 * bind to the devices as they are plugged in.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbh.h>

LOG_MODULE_REGISTER(drv_usbh, CONFIG_LOG_DEFAULT_LEVEL);

USBH_CONTROLLER_DEFINE(drv_usbh_ctx, DEVICE_DT_GET(DT_NODELABEL(zephyr_uhc0)));

static int drv_usbh_init(void)
{
    int ret;

    ret = usbh_init(&drv_usbh_ctx);
    if (ret != 0) {
        LOG_ERR("USB host init failed (%d)", ret);
        return ret;
    }

    ret = usbh_enable(&drv_usbh_ctx);
    if (ret != 0) {
        LOG_ERR("USB host enable failed (%d)", ret);
    }

    return ret;
}

SYS_INIT(drv_usbh_init, APPLICATION, CONFIG_DRV_USBH_INIT_PRIORITY);

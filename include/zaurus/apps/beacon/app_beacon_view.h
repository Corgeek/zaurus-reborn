/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZAURUS_INCLUDE_ZAURUS_APPS_BEACON_APP_BEACON_VIEW_H_
#define ZAURUS_INCLUDE_ZAURUS_APPS_BEACON_APP_BEACON_VIEW_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start Bluetooth for the "ble beacon" command
 *
 * Waits for a USB Bluetooth dongle on the USB host port, trying again each
 * time the HCI transport gives up, and starts scanning. Call it from the
 * main thread, as the usbh_ble_beacon sample does.
 *
 * @retval 0 Scanning
 * @retval -errno Bluetooth failed to start for another reason than a
 *         missing dongle
 */
int app_beacon_start(void);

#ifdef __cplusplus
}
#endif

#endif /* ZAURUS_INCLUDE_ZAURUS_APPS_BEACON_APP_BEACON_VIEW_H_ */

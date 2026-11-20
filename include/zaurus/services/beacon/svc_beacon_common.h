/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZAURUS_INCLUDE_ZAURUS_SERVICES_BEACON_SVC_BEACON_COMMON_H_
#define ZAURUS_INCLUDE_ZAURUS_SERVICES_BEACON_SVC_BEACON_COMMON_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/bluetooth/addr.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest device name kept, without the terminating NUL */
#define SVC_BEACON_NAMELEN 20

/** Where the expected RSSI at 1 m comes from */
enum svc_beacon_kind {
    /** No calibration in the advertisement: CONFIG_SVC_BEACON_DEFAULT_TXPOWER */
    SVC_BEACON_KIND_OTHER,
    /** iBeacon measured power */
    SVC_BEACON_KIND_IBEACON,
    /** Eddystone UID or URL Tx power at 0 m, less 41 dB */
    SVC_BEACON_KIND_EDDYSTONE,
    /** TX Power Level AD type, less 41 dB */
    SVC_BEACON_KIND_TXPOWER,
};

/** Coarse distance class */
enum svc_beacon_range {
    /** Closer than CONFIG_SVC_BEACON_IMMEDIATE_CM */
    SVC_BEACON_RANGE_IMMEDIATE,
    /** Closer than CONFIG_SVC_BEACON_NEAR_CM */
    SVC_BEACON_RANGE_NEAR,
    /** Further away */
    SVC_BEACON_RANGE_FAR,
};

/** One advertiser, as returned by svc_beacon_list() */
struct svc_beacon_entry {
    /** Advertiser address */
    bt_addr_le_t addr;
    /** Complete or shortened local name, empty if none was advertised */
    char name[SVC_BEACON_NAMELEN + 1];
    /** Source of txpower */
    enum svc_beacon_kind kind;
    /** Smoothed RSSI in dBm */
    int8_t rssi;
    /** Expected RSSI at 1 m in dBm */
    int8_t txpower;
    /** Estimated distance in centimeters */
    uint32_t distance;
    /** Distance class of distance */
    enum svc_beacon_range range;
    /** Time since the last advertisement, in milliseconds */
    uint32_t age;
    /** Number of advertisements received */
    uint32_t reports;
};

/**
 * @brief Start Bluetooth and scan for advertisers
 *
 * Enables Bluetooth and starts a passive scan without duplicate
 * filtering, so that every advertisement updates the RSSI. Enabling waits
 * for the HCI transport, which for a USB controller means until one is
 * plugged in or the transport gives up; the call can then be repeated.
 *
 * @retval 0 Scanning
 * @retval -errno bt_enable() or bt_le_scan_start() failed
 */
int svc_beacon_start(void);

/**
 * @brief Advertisers seen recently, nearest first
 *
 * Advertisers not heard from for CONFIG_SVC_BEACON_TIMEOUT_MS are dropped.
 *
 * @param list Filled with up to @p max entries
 * @param max Number of entries @p list can hold
 *
 * @return Number of entries written to @p list
 */
size_t svc_beacon_list(struct svc_beacon_entry *list, size_t max);

/**
 * @brief Number of advertisements received since svc_beacon_start()
 *
 * @return Advertisement count
 */
uint32_t svc_beacon_reports(void);

/**
 * @brief Short name of a kind, for display
 *
 * @param kind Kind
 *
 * @return Name, at most 5 characters
 */
const char *svc_beacon_kindname(enum svc_beacon_kind kind);

/**
 * @brief Name of a distance class, for display
 *
 * @param range Distance class
 *
 * @return Name, at most 9 characters
 */
const char *svc_beacon_rangename(enum svc_beacon_range range);

#ifdef __cplusplus
}
#endif

#endif /* ZAURUS_INCLUDE_ZAURUS_SERVICES_BEACON_SVC_BEACON_COMMON_H_ */

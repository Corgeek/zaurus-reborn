/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Distance to Bluetooth LE advertisers
 *
 * A passive scan keeps a table of the advertisers heard recently. The RSSI
 * of each is smoothed with an exponential moving average and turned into a
 * distance with the log-distance path loss model:
 *
 *     distance = 1 m * 10 ^ ((txpower - rssi) / (10 * n))
 *
 * where txpower is the RSSI expected at 1 m and n the path loss exponent
 * (2 in free space, 2.5 to 4 indoors). iBeacon and Eddystone carry txpower
 * in their advertisements; the TX Power Level AD type gives the power at
 * 0 m, 41 dB above the value at 1 m. Other advertisers get a default.
 * Walls, bodies and the antenna orientation easily move the RSSI by 10 dB,
 * so the distance is a rough figure and the range class is what to trust.
 */

#include <math.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <zaurus/services/beacon/svc_beacon_common.h>

LOG_MODULE_REGISTER(svc_beacon, CONFIG_LOG_DEFAULT_LEVEL);

/* Loss over the first meter, between the power at 0 m and at 1 m */
#define SVC_BEACON_LOSS_1M 41

/* RSSI value meaning "not available" */
#define SVC_BEACON_RSSI_NONE 127

/* Fixed point scale of the smoothed RSSI */
#define SVC_BEACON_RSSI_SCALE 16

/* Longest distance reported, in centimeters */
#define SVC_BEACON_DISTANCE_MAX 99900U

struct svc_beacon_slot {
    bool used;
    bt_addr_le_t addr;
    char name[SVC_BEACON_NAMELEN + 1];
    enum svc_beacon_kind kind;
    int8_t txpower;
    /* Smoothed RSSI times SVC_BEACON_RSSI_SCALE */
    int32_t rssi;
    int64_t seen;
    uint32_t reports;
};

/* What one advertisement tells about its sender */
struct svc_beacon_adinfo {
    char name[SVC_BEACON_NAMELEN + 1];
    enum svc_beacon_kind kind;
    int8_t txpower;
};

static struct svc_beacon_slot svc_beacon_table[CONFIG_SVC_BEACON_MAX_DEVICES];
static uint32_t svc_beacon_count;
static K_MUTEX_DEFINE(svc_beacon_lock);

/* A stronger calibration source replaces a weaker one */
static void svc_beacon_setpower(struct svc_beacon_adinfo *info, enum svc_beacon_kind kind,
                                int8_t txpower)
{
    if (info->kind == SVC_BEACON_KIND_OTHER || kind < info->kind) {
        info->kind = kind;
        info->txpower = txpower;
    }
}

static bool svc_beacon_parsead(struct bt_data *data, void *user_data)
{
    struct svc_beacon_adinfo *info = user_data;
    const uint8_t *d = data->data;

    switch (data->type) {
    case BT_DATA_NAME_COMPLETE:
    case BT_DATA_NAME_SHORTENED: {
        size_t len = MIN(data->data_len, SVC_BEACON_NAMELEN);

        for (size_t i = 0; i < len; i++) {
            info->name[i] = (d[i] >= 0x20U && d[i] < 0x7fU) ? (char)d[i] : '?';
        }
        info->name[len] = '\0';
        break;
    }
    case BT_DATA_MANUFACTURER_DATA:
        /* Apple company ID, iBeacon type 0x02 and length 0x15, measured power last */
        if (data->data_len >= 25U && d[0] == 0x4cU && d[1] == 0x00U && d[2] == 0x02U &&
            d[3] == 0x15U) {
            svc_beacon_setpower(info, SVC_BEACON_KIND_IBEACON, (int8_t)d[24]);
        }
        break;
    case BT_DATA_SVC_DATA16:
        /* Eddystone service 0xfeaa, UID (0x00) or URL (0x10) frame, power at 0 m */
        if (data->data_len >= 4U && d[0] == 0xaaU && d[1] == 0xfeU &&
            (d[2] == 0x00U || d[2] == 0x10U)) {
            svc_beacon_setpower(info, SVC_BEACON_KIND_EDDYSTONE,
                                (int8_t)((int8_t)d[3] - SVC_BEACON_LOSS_1M));
        }
        break;
    case BT_DATA_TX_POWER:
        if (data->data_len >= 1U) {
            svc_beacon_setpower(info, SVC_BEACON_KIND_TXPOWER,
                                (int8_t)((int8_t)d[0] - SVC_BEACON_LOSS_1M));
        }
        break;
    default:
        break;
    }

    return true;
}

/* The slot of addr, or a free one, or the one heard from longest ago */
static struct svc_beacon_slot *svc_beacon_findslot(const bt_addr_le_t *addr)
{
    struct svc_beacon_slot *victim = NULL;

    for (size_t i = 0; i < ARRAY_SIZE(svc_beacon_table); i++) {
        struct svc_beacon_slot *slot = &svc_beacon_table[i];

        if (slot->used && bt_addr_le_eq(&slot->addr, addr)) {
            return slot;
        }

        if (victim == NULL || (victim->used && (!slot->used || slot->seen < victim->seen))) {
            victim = slot;
        }
    }

    victim->used = false;

    return victim;
}

static void svc_beacon_report(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
                              struct net_buf_simple *ad)
{
    struct svc_beacon_adinfo info = {
        .kind = SVC_BEACON_KIND_OTHER,
        .txpower = CONFIG_SVC_BEACON_DEFAULT_TXPOWER,
    };
    struct svc_beacon_slot *slot;

    ARG_UNUSED(type);

    if (rssi == SVC_BEACON_RSSI_NONE) {
        return;
    }

    bt_data_parse(ad, svc_beacon_parsead, &info);

    k_mutex_lock(&svc_beacon_lock, K_FOREVER);

    svc_beacon_count++;
    slot = svc_beacon_findslot(addr);

    if (!slot->used) {
        memset(slot, 0, sizeof(*slot));
        slot->used = true;
        bt_addr_le_copy(&slot->addr, addr);
        slot->kind = SVC_BEACON_KIND_OTHER;
        slot->txpower = CONFIG_SVC_BEACON_DEFAULT_TXPOWER;
        slot->rssi = (int32_t)rssi * SVC_BEACON_RSSI_SCALE;
    } else {
        slot->rssi += (((int32_t)rssi * SVC_BEACON_RSSI_SCALE) - slot->rssi) /
                      CONFIG_SVC_BEACON_RSSI_SMOOTHING;
    }

    /* Advertisers alternate between packets: keep what any of them told */
    if (info.name[0] != '\0') {
        memcpy(slot->name, info.name, sizeof(slot->name));
    }

    if (info.kind != SVC_BEACON_KIND_OTHER) {
        slot->kind = info.kind;
        slot->txpower = info.txpower;
    }

    slot->seen = k_uptime_get();
    slot->reports++;

    k_mutex_unlock(&svc_beacon_lock);
}

static uint32_t svc_beacon_distance(int8_t txpower, int8_t rssi)
{
    /* 10 ^ ((txpower - rssi) / (10 * n)), with n given in tenths */
    float exponent = (float)(txpower - rssi) / (float)CONFIG_SVC_BEACON_PATH_LOSS_X10;
    float distance = 100.0f * powf(10.0f, exponent);

    if (distance >= (float)SVC_BEACON_DISTANCE_MAX) {
        return SVC_BEACON_DISTANCE_MAX;
    }

    return (uint32_t)distance;
}

static enum svc_beacon_range svc_beacon_range(uint32_t distance)
{
    if (distance < CONFIG_SVC_BEACON_IMMEDIATE_CM) {
        return SVC_BEACON_RANGE_IMMEDIATE;
    }

    if (distance < CONFIG_SVC_BEACON_NEAR_CM) {
        return SVC_BEACON_RANGE_NEAR;
    }

    return SVC_BEACON_RANGE_FAR;
}

size_t svc_beacon_list(struct svc_beacon_entry *list, size_t max)
{
    int64_t now = k_uptime_get();
    size_t count = 0;

    k_mutex_lock(&svc_beacon_lock, K_FOREVER);

    for (size_t i = 0; i < ARRAY_SIZE(svc_beacon_table); i++) {
        struct svc_beacon_slot *slot = &svc_beacon_table[i];
        struct svc_beacon_entry entry;
        size_t pos;

        if (!slot->used) {
            continue;
        }

        if (now - slot->seen > CONFIG_SVC_BEACON_TIMEOUT_MS) {
            slot->used = false;
            continue;
        }

        bt_addr_le_copy(&entry.addr, &slot->addr);
        memcpy(entry.name, slot->name, sizeof(entry.name));
        entry.kind = slot->kind;
        entry.rssi = (int8_t)(slot->rssi / SVC_BEACON_RSSI_SCALE);
        entry.txpower = slot->txpower;
        entry.distance = svc_beacon_distance(entry.txpower, entry.rssi);
        entry.range = svc_beacon_range(entry.distance);
        entry.age = (uint32_t)(now - slot->seen);
        entry.reports = slot->reports;

        /* Insertion sort, nearest first; the furthest falls off a full list */
        pos = count;
        while (pos > 0 && list[pos - 1].distance > entry.distance) {
            pos--;
        }

        if (pos >= max) {
            continue;
        }

        if (count < max) {
            count++;
        }

        memmove(&list[pos + 1], &list[pos], (count - 1 - pos) * sizeof(list[0]));
        list[pos] = entry;
    }

    k_mutex_unlock(&svc_beacon_lock);

    return count;
}

uint32_t svc_beacon_reports(void)
{
    uint32_t count;

    k_mutex_lock(&svc_beacon_lock, K_FOREVER);
    count = svc_beacon_count;
    k_mutex_unlock(&svc_beacon_lock);

    return count;
}

const char *svc_beacon_kindname(enum svc_beacon_kind kind)
{
    switch (kind) {
    case SVC_BEACON_KIND_IBEACON:
        return "iBcn";
    case SVC_BEACON_KIND_EDDYSTONE:
        return "Eddy";
    case SVC_BEACON_KIND_TXPOWER:
        return "TxPwr";
    default:
        return "-";
    }
}

const char *svc_beacon_rangename(enum svc_beacon_range range)
{
    switch (range) {
    case SVC_BEACON_RANGE_IMMEDIATE:
        return "immediate";
    case SVC_BEACON_RANGE_NEAR:
        return "near";
    default:
        return "far";
    }
}

int svc_beacon_start(void)
{
    struct bt_le_scan_param param = {
        .type = BT_LE_SCAN_TYPE_PASSIVE,
        .options = BT_LE_SCAN_OPT_NONE,
        /* Listen all the time, for as many RSSI samples as possible */
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_INTERVAL,
    };
    int ret;

    ret = bt_enable(NULL);
    if (ret != 0) {
        LOG_ERR("Bluetooth enable failed (%d)", ret);
        return ret;
    }

    ret = bt_le_scan_start(&param, svc_beacon_report);
    if (ret != 0) {
        LOG_ERR("Scan start failed (%d)", ret);
    }

    return ret;
}

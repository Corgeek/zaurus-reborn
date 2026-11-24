/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief "ble beacon" shell command: distance to Bluetooth LE beacons
 *
 * app_beacon_start(), called from main(), starts Bluetooth the way the
 * usbh_ble_beacon sample does: from the main thread, trying again for as
 * long as no USB Bluetooth dongle answers. The scan (services/beacon) then
 * runs in the background.
 *
 * The "ble beacon" command takes over the LCD like top: it switches the
 * terminal to its alternate screen and lists the advertisers nearest
 * first, once a second, each with a bar as long as its estimated distance
 * on a logarithmic axis from 10 cm to 100 m, the smoothed RSSI, the name,
 * the range class (immediate, near, far) and the expected RSSI at 1 m.
 * q (or Cancel) goes back to the shell screen as it was. Log output stays
 * off the LCD meanwhile, so that it does not scroll the list away.
 */

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zaurus/apps/beacon/app_beacon_view.h>
#include <zaurus/drivers/term/drv_term_common.h>
#include <zaurus/services/beacon/svc_beacon_common.h>

#define APP_BEACON_MAXCOLS 128
#define APP_BEACON_MAXROWS 48
/* Title, settings, axis labels and axis above the list */
#define APP_BEACON_TOPROWS 4
/* Footer below the list */
#define APP_BEACON_BOTTOMROWS 1
/* Screen rows per advertiser: address and bar, then name and details */
#define APP_BEACON_ENTRYROWS 2

/* Left of the bars: an address and two spaces */
#define APP_BEACON_LEFTCOLS 19
/*
 * Right of the bars: " %6s %4d" with the distance and the RSSI, and the last
 * column left free, as the erase after a line that fills it would clear it
 */
#define APP_BEACON_RIGHTCOLS 13

/* The bar axis is logarithmic, from 10 cm over three decades to 100 m */
#define APP_BEACON_AXISMIN_CM 10.0f
#define APP_BEACON_AXISDECADES 3

/* Screen refresh, and how often a key is looked for in between */
#define APP_BEACON_REFRESH_MS 1000
#define APP_BEACON_KEYPOLL_MS 50

/* Alternate screen and hidden cursor on entry, the reverse on exit */
#define APP_BEACON_ENTER "\x1b[?1049h\x1b[?25l"
#define APP_BEACON_LEAVE "\x1b[?25h\x1b[?1049l"

#define APP_BEACON_CTRL_C 0x03

/* Waiting for a dongle */
#define APP_BEACON_WAITING 1
/* 0 once scanning, a negative errno if Bluetooth failed to start */
static atomic_t app_beacon_status = ATOMIC_INIT(APP_BEACON_WAITING);

/* Write one screen row, cut to the screen width, and clear the rest of it */
static void app_beacon_putline(uint16_t row, uint16_t cols, const char *fmt, ...)
{
    char line[APP_BEACON_MAXCOLS + 16];
    size_t width = MIN(cols, APP_BEACON_MAXCOLS);
    va_list ap;
    int len;

    len = snprintf(line, sizeof(line), "\x1b[%u;1H", row);

    va_start(ap, fmt);
    (void)vsnprintf(&line[len], width + 1U, fmt, ap);
    va_end(ap);

    strcat(line, "\x1b[K");
    drv_term_write(line, strlen(line));
}

static void app_beacon_fmtdistance(char *buf, size_t size, uint32_t distance)
{
    if (distance >= 99900U) {
        (void)snprintf(buf, size, ">999m");
    } else {
        (void)snprintf(buf, size, "%u.%um", distance / 100U, (distance % 100U) / 10U);
    }
}

/* Column of a distance on a bar of width columns, 0 to width - 1 */
static size_t app_beacon_barcol(uint32_t distance, size_t width)
{
    float decades = log10f(MAX((float)distance, APP_BEACON_AXISMIN_CM) / APP_BEACON_AXISMIN_CM);
    float col = decades * (float)(width - 1U) / (float)APP_BEACON_AXISDECADES + 0.5f;

    return MIN((size_t)col, width - 1U);
}

/*
 * The bar of one advertiser, up to its distance, with the range class
 * boundaries showing as ':' in the empty part
 */
static void app_beacon_fmtbar(char *buf, size_t width, uint32_t distance)
{
    size_t len = app_beacon_barcol(distance, width) + 1U;
    size_t immediate = app_beacon_barcol(CONFIG_SVC_BEACON_IMMEDIATE_CM, width);
    size_t near = app_beacon_barcol(CONFIG_SVC_BEACON_NEAR_CM, width);

    for (size_t i = 0; i < width; i++) {
        if (i < len) {
            buf[i] = '#';
        } else if (i == immediate || i == near) {
            buf[i] = ':';
        } else {
            buf[i] = ' ';
        }
    }
    buf[width] = '\0';
}

/* Decade labels over the bars, and the axis with a '+' at each decade */
static void app_beacon_fmtaxis(char *labels, char *axis, size_t width)
{
    static const char *const names[APP_BEACON_AXISDECADES + 1] = {"0.1m", "1m", "10m", "100m"};
    uint32_t distance = (uint32_t)APP_BEACON_AXISMIN_CM;

    memset(labels, ' ', width);
    memset(axis, '-', width);
    labels[width] = '\0';
    axis[width] = '\0';

    for (size_t i = 0; i < ARRAY_SIZE(names); i++, distance *= 10U) {
        size_t col = app_beacon_barcol(distance, width);
        size_t len = strlen(names[i]);
        /* Centered on the tick, but kept inside the axis */
        size_t start = MIN(col - MIN(col, len / 2U), width - len);

        memcpy(&labels[start], names[i], len);
        axis[col] = '+';
    }
}

static void app_beacon_draw(uint16_t cols, uint16_t rows)
{
    static struct svc_beacon_entry list[APP_BEACON_MAXROWS / APP_BEACON_ENTRYROWS];
    static char bar[APP_BEACON_MAXCOLS + 1];
    static char axis[APP_BEACON_MAXCOLS + 1];
    size_t width = MIN((size_t)cols, APP_BEACON_MAXCOLS) - APP_BEACON_LEFTCOLS -
                   APP_BEACON_RIGHTCOLS;
    size_t slots = MIN(((size_t)rows - APP_BEACON_TOPROWS - APP_BEACON_BOTTOMROWS) /
                           APP_BEACON_ENTRYROWS,
                       ARRAY_SIZE(list));
    atomic_val_t status = atomic_get(&app_beacon_status);
    size_t count = 0;
    uint16_t row = 1;

    if (status == 0) {
        count = svc_beacon_list(list, slots);
        app_beacon_putline(row++, cols, "BLE beacons: %zu in range, %u adverts received",
                           count, svc_beacon_reports());
    } else if (status == APP_BEACON_WAITING) {
        app_beacon_putline(row++, cols, "BLE beacons: waiting for a USB Bluetooth dongle");
    } else {
        app_beacon_putline(row++, cols, "BLE beacons: Bluetooth failed to start (%d)",
                           (int)status);
    }

    app_beacon_putline(row++, cols, "n=%d.%d  default 1m %d dBm  ':' immediate <%d cm, near <%d cm",
                       CONFIG_SVC_BEACON_PATH_LOSS_X10 / 10, CONFIG_SVC_BEACON_PATH_LOSS_X10 % 10,
                       CONFIG_SVC_BEACON_DEFAULT_TXPOWER, CONFIG_SVC_BEACON_IMMEDIATE_CM,
                       CONFIG_SVC_BEACON_NEAR_CM);

    app_beacon_fmtaxis(bar, axis, width);
    app_beacon_putline(row++, cols, "%-*s%s", APP_BEACON_LEFTCOLS, "", bar);
    app_beacon_putline(row++, cols, "%-*s%s %6s %4s", APP_BEACON_LEFTCOLS, "Address / Name", axis,
                       "Dist", "RSSI");

    for (size_t i = 0; i < slots; i++) {
        char addr[BT_ADDR_STR_LEN];
        char distance[8];

        if (i >= count) {
            app_beacon_putline(row++, cols, "");
            app_beacon_putline(row++, cols, "");
            continue;
        }

        bt_addr_to_str(&list[i].addr.a, addr, sizeof(addr));
        app_beacon_fmtdistance(distance, sizeof(distance), list[i].distance);
        app_beacon_fmtbar(bar, width, list[i].distance);

        app_beacon_putline(row++, cols, "%-*s%s %6s %4d", APP_BEACON_LEFTCOLS, addr, bar,
                           distance, list[i].rssi);
        app_beacon_putline(row++, cols, "  %-*s %-9s %-5s 1m %d dBm",
                           SVC_BEACON_NAMELEN, (list[i].name[0] != '\0') ? list[i].name : "-",
                           svc_beacon_rangename(list[i].range),
                           svc_beacon_kindname(list[i].kind), list[i].txpower);
    }

    app_beacon_putline(row, cols, "Distance = 1m * 10^((1m - RSSI) / 10n).  q: back to the shell");
}

/*
 * Wait up to the refresh period for q or Cancel. The shell thread runs this
 * command, so nothing else reads the shell's input meanwhile.
 */
static bool app_beacon_waitquit(const struct shell *sh)
{
    for (int waited = 0; waited < APP_BEACON_REFRESH_MS; waited += APP_BEACON_KEYPOLL_MS) {
        char c;
        size_t cnt;

        while (sh->iface->api->read(sh->iface, &c, 1U, &cnt) == 0 && cnt != 0U) {
            if (c == 'q' || c == 'Q' || c == APP_BEACON_CTRL_C) {
                return true;
            }
        }

        k_msleep(APP_BEACON_KEYPOLL_MS);
    }

    return false;
}

static int app_beacon_shellcmd(const struct shell *sh, size_t argc, char **argv)
{
    uint16_t cols;
    uint16_t rows;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (drv_term_size(&cols, &rows) != 0 ||
        rows < APP_BEACON_TOPROWS + APP_BEACON_BOTTOMROWS + APP_BEACON_ENTRYROWS ||
        cols <= APP_BEACON_LEFTCOLS + APP_BEACON_RIGHTCOLS + 4U) {
        shell_error(sh, "No room on the display");
        return -ENOEXEC;
    }

    drv_term_write(APP_BEACON_ENTER, strlen(APP_BEACON_ENTER));

    do {
        app_beacon_draw(cols, rows);
    } while (!app_beacon_waitquit(sh));

    drv_term_write(APP_BEACON_LEAVE, strlen(APP_BEACON_LEAVE));

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(app_beacon_blecmds,
    SHELL_CMD_ARG(beacon, NULL, "Show the distance to BLE beacons; q to quit",
                  app_beacon_shellcmd, 1, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(ble, &app_beacon_blecmds, "Bluetooth LE commands", NULL);

int app_beacon_start(void)
{
    int ret;

    /* The HCI transport gives up after a while without a dongle: try again */
    do {
        ret = svc_beacon_start();
    } while (ret == -ENODEV);

    atomic_set(&app_beacon_status, ret);

    return ret;
}

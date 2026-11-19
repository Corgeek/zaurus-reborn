/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Shell backend on the terminal and a keyboard
 *
 * Shell output is written to the terminal; with the UART
 * console enabled it is copied to the UART as well. Input comes from
 * INPUT_EV_KEY events of any input device, turned into text by
 * drv_kbd_feedtext().
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/ring_buffer.h>

#include <zaurus/drivers/term/drv_term_common.h>
#include <zaurus/drivers/kbd/drv_kbd_common.h>

struct drv_shell_state {
    shell_transport_handler_t handler;
    void *context;
    struct drv_kbd_state text;
};

static struct drv_shell_state state;
static struct k_spinlock rx_lock;
RING_BUF_DECLARE(rx_ring, CONFIG_DRV_SHELL_RX_BUF_SIZE);

static void drv_shell_push(const char *s, size_t len)
{
    k_spinlock_key_t key = k_spin_lock(&rx_lock);
    uint32_t put = ring_buf_put(&rx_ring, (const uint8_t *)s, len);

    k_spin_unlock(&rx_lock, key);

    if (put != 0U && state.handler != NULL) {
        state.handler(SHELL_TRANSPORT_EVT_RX_RDY, state.context);
    }
}

static void drv_shell_keyevent(struct input_event *evt, void *user_data)
{
    char buf[DRV_KBD_MAXLEN];
    size_t len;

    ARG_UNUSED(user_data);

    len = drv_kbd_feedtext(&state.text, evt, buf);
    if (len != 0U) {
        drv_shell_push(buf, len);
    }
}

INPUT_CALLBACK_DEFINE(NULL, drv_shell_keyevent, NULL);

static int drv_shell_init(const struct shell_transport *transport, const void *config,
                          shell_transport_handler_t evt_handler, void *context)
{
    ARG_UNUSED(transport);
    ARG_UNUSED(config);

    state.context = context;
    state.handler = evt_handler;

    return 0;
}

static int drv_shell_uninit(const struct shell_transport *transport)
{
    ARG_UNUSED(transport);

    state.handler = NULL;

    return 0;
}

static int drv_shell_enable(const struct shell_transport *transport, bool blocking)
{
    ARG_UNUSED(transport);
    ARG_UNUSED(blocking);

    return 0;
}

static int drv_shell_write(const struct shell_transport *transport, const void *data, size_t length,
                           size_t *cnt)
{
    ARG_UNUSED(transport);

    drv_term_write(data, length);

#ifdef CONFIG_UART_CONSOLE
    const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

    for (size_t i = 0U; i < length; i++) {
        uart_poll_out(uart, ((const uint8_t *)data)[i]);
    }
#endif

    *cnt = length;

    return 0;
}

static int drv_shell_read(const struct shell_transport *transport, void *data, size_t length,
                          size_t *cnt)
{
    k_spinlock_key_t key;

    ARG_UNUSED(transport);

    key = k_spin_lock(&rx_lock);
    *cnt = ring_buf_get(&rx_ring, data, length);
    k_spin_unlock(&rx_lock, key);

    return 0;
}

static const struct shell_transport_api kbd_display_api = {
    .init = drv_shell_init,
    .uninit = drv_shell_uninit,
    .enable = drv_shell_enable,
    .write = drv_shell_write,
    .read = drv_shell_read,
};

static struct shell_transport drv_shell_transport = {
    .api = &kbd_display_api,
    .ctx = &state,
};

SHELL_DEFINE(drv_shell, CONFIG_DRV_SHELL_PROMPT, &drv_shell_transport,
             256, 0, SHELL_FLAG_OLF_CRLF);

static int drv_shell_start(void)
{
    static const struct shell_backend_config_flags cfg_flags =
        SHELL_DEFAULT_BACKEND_CONFIG_FLAGS;

    /* Log output already reaches the display through the console */
    return shell_init(&drv_shell, NULL, cfg_flags, false, 0U);
}

SYS_INIT(drv_shell_start, APPLICATION, 0);

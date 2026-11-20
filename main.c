/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zaurus SL-C3x00 application: the smallest one, blinking the green LED.
 *
 * The samples are on the samples/<name> branches, each started from this
 * one.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
    static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

    printk("Zaurus LED blink on %s\n", CONFIG_BOARD_TARGET);

    if (!gpio_is_ready_dt(&led) || gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) != 0) {
        printk("LED not available\n");
        return 0;
    }

    while (true) {
        (void)gpio_pin_toggle_dt(&led);
        k_msleep(500);
    }

    return 0;
}

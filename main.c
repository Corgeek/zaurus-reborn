/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zaurus SL-C3x00 application: LCD terminal.
 *
 * Shell on the LCD, typed on the machine's keyboard, with the i2c, gpio,
 * input and device commands built in. Output is copied to the UART. The
 * green LED blinks to show the system is alive. "input dump on" shows the
 * key and switch events.
 *
 * Holding the power key for a second, or the poweroff command, powers the
 * machine off (services/power); the power key starts it again.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
    static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
    bool led_ok = gpio_is_ready_dt(&led) && (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) == 0);

    printk("Zaurus LCD shell on %s\n", CONFIG_BOARD_TARGET);
    printk("i2c, gpio, input and device commands; Tab completes names\n");
    printk("Fn+- @   Shift+, /   Shift+. ?   Fn+B _   Fn+E =\n");
    printk("Fn+G ;   Fn+H :   Fn+T [   Fn+Y ]   Cancel: Ctrl-C\n");

    while (led_ok) {
        (void)gpio_pin_toggle_dt(&led);
        k_msleep(500);
    }

    return 0;
}

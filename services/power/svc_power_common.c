/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Power service
 *
 * Holding the power key, or the poweroff shell command, powers the machine
 * off; the power key starts it again.
 */

#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/printk.h>

#include <zaurus/services/power/svc_power_common.h>

#define POWER_KEY_HOLD K_MSEC(CONFIG_SVC_POWER_KEY_HOLD_MS)

void svc_power_off(void)
{
    //printk("Powering off; press the power key to start again\n");
    /* printk goes through the deferred log: flush it first */

    log_panic();
    sys_poweroff();
}

static void svc_power_keyheld(struct k_work *work)
{
    ARG_UNUSED(work);

    svc_power_off();
}
static K_WORK_DELAYABLE_DEFINE(svc_power_keywork, svc_power_keyheld);

static void svc_power_keyevent(struct input_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);

    if (evt->type != INPUT_EV_KEY || evt->code != INPUT_KEY_POWER) {
        return;
    }

    if (evt->value != 0) {
        (void)k_work_schedule(&svc_power_keywork, POWER_KEY_HOLD);
    } else {
        (void)k_work_cancel_delayable(&svc_power_keywork);
    }
}
INPUT_CALLBACK_DEFINE(NULL, svc_power_keyevent, NULL);

#ifdef CONFIG_SHELL
static int svc_power_shellcmd(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(sh);
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    svc_power_off();

    return 0;
}
SHELL_CMD_REGISTER(poweroff, NULL, "Power the machine off", svc_power_shellcmd);
#endif /* CONFIG_SHELL */

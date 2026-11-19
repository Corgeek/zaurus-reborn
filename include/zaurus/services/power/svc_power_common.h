/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZAURUS_INCLUDE_ZAURUS_SERVICES_POWER_SVC_POWER_COMMON_H_
#define ZAURUS_INCLUDE_ZAURUS_SERVICES_POWER_SVC_POWER_COMMON_H_

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Power the machine off
 *
 * Flushes the log and powers off; the power key starts the machine again.
 */
FUNC_NORETURN void svc_power_off(void);

#ifdef __cplusplus
}
#endif

#endif /* ZAURUS_INCLUDE_ZAURUS_SERVICES_POWER_SVC_POWER_COMMON_H_ */

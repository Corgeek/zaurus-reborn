/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZAURUS_INCLUDE_ZAURUS_DRIVERS_TERM_DRV_TERM_COMMON_H_
#define ZAURUS_INCLUDE_ZAURUS_DRIVERS_TERM_DRV_TERM_COMMON_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Write text to the terminal
 *
 * The text goes to the display only, not to the UART. A subset of VT100
 * control sequences is interpreted: cursor movement and positioning
 * (CSI A, B, C, D, H, f), erasing (CSI J, K) and saving and restoring the
 * cursor (ESC 7, ESC 8). Other sequences are skipped.
 *
 * @param buf Text to write
 * @param len Number of bytes in @p buf
 */
void drv_term_write(const char *buf, size_t len);

/**
 * @brief Text console size in characters
 *
 * @param cols Set to the number of columns, if not NULL
 * @param rows Set to the number of rows, if not NULL
 *
 * @retval 0 Success
 * @retval -ENODEV The console is not running
 */
int drv_term_size(uint16_t *cols, uint16_t *rows);

#ifdef __cplusplus
}
#endif

#endif /* ZAURUS_INCLUDE_ZAURUS_DRIVERS_TERM_DRV_TERM_COMMON_H_ */

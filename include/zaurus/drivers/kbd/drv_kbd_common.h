/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZAURUS_INCLUDE_ZAURUS_DRIVERS_KBD_DRV_KBD_COMMON_H_
#define ZAURUS_INCLUDE_ZAURUS_DRIVERS_KBD_DRV_KBD_COMMON_H_

#include <stdbool.h>
#include <stddef.h>
#include <zephyr/input/input.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Longest output of drv_kbd_feedtext(), in bytes */
#define DRV_KBD_MAXLEN 4U

/** Modifier state of one keyboard */
struct drv_kbd_state {
    bool shift_l;
    bool shift_r;
    bool ctrl;
    bool fn;
};

/**
 * @brief Turn a key event into text
 *
 * Tracks the modifiers and, for a key press, writes the character or VT100
 * sequence it stands for on the Zaurus C3x00 keycaps:
 *
 * - Shift with a digit gives !"#$%&'()~, Shift with , and . gives / and ?.
 * - Fn gives @ on -, ^ = + [ ] { } on W E R T Y U I, ; : * \ | on
 *   G H J K L, _ on B and < > on , and . .
 * - Ctrl with a letter gives the control character.
 * - OK, the jog dial's OK and Enter give CR; Cancel and the jog dial's
 *   Cancel give Ctrl-C.
 * - The cursor keys and the jog dial give the VT100 cursor sequences, and
 *   Fn with Backspace gives Delete (CSI 3 ~).
 *
 * @param state Modifier state, zero-initialized before the first event
 * @param evt Input event; events other than INPUT_EV_KEY give nothing
 * @param buf Output, at least @ref DRV_KBD_MAXLEN bytes, not
 *            NUL-terminated
 *
 * @return Number of bytes written to @p buf, 0 when the event gives no text
 */
size_t drv_kbd_feedtext(struct drv_kbd_state *state, const struct input_event *evt,
                        char *buf);

#ifdef __cplusplus
}
#endif

#endif /* ZAURUS_INCLUDE_ZAURUS_DRIVERS_KBD_DRV_KBD_COMMON_H_ */

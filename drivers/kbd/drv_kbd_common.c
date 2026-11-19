/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Text from Zaurus C3x00 key events
 *
 * The C3x00 keyboard has no keys of its own for most symbols: they are
 * printed on the keycaps and typed with Shift or Fn. QEMU's spitz keyboard
 * has the same layout, except for @, which it puts on Shift with -.
 */

#include <string.h>
#include <zephyr/sys/util.h>

#include <zaurus/drivers/kbd/drv_kbd_common.h>

#define FN_KEY      (CONFIG_DRV_KBD_FN_KEY)
#define NUM_KEYS    (128U)

#define CHAR_DEL    (0x7f)
#define CHAR_CTRLC  (0x03)

static const char plain_map[NUM_KEYS] = {
    [INPUT_KEY_1] = '1', [INPUT_KEY_2] = '2', [INPUT_KEY_3] = '3', [INPUT_KEY_4] = '4',
    [INPUT_KEY_5] = '5', [INPUT_KEY_6] = '6', [INPUT_KEY_7] = '7', [INPUT_KEY_8] = '8',
    [INPUT_KEY_9] = '9', [INPUT_KEY_0] = '0', [INPUT_KEY_MINUS] = '-',
    [INPUT_KEY_Q] = 'q', [INPUT_KEY_W] = 'w', [INPUT_KEY_E] = 'e', [INPUT_KEY_R] = 'r',
    [INPUT_KEY_T] = 't', [INPUT_KEY_Y] = 'y', [INPUT_KEY_U] = 'u', [INPUT_KEY_I] = 'i',
    [INPUT_KEY_O] = 'o', [INPUT_KEY_P] = 'p', [INPUT_KEY_A] = 'a', [INPUT_KEY_S] = 's',
    [INPUT_KEY_D] = 'd', [INPUT_KEY_F] = 'f', [INPUT_KEY_G] = 'g', [INPUT_KEY_H] = 'h',
    [INPUT_KEY_J] = 'j', [INPUT_KEY_K] = 'k', [INPUT_KEY_L] = 'l', [INPUT_KEY_Z] = 'z',
    [INPUT_KEY_X] = 'x', [INPUT_KEY_C] = 'c', [INPUT_KEY_V] = 'v', [INPUT_KEY_B] = 'b',
    [INPUT_KEY_N] = 'n', [INPUT_KEY_M] = 'm', [INPUT_KEY_COMMA] = ',', [INPUT_KEY_DOT] = '.',
    [INPUT_KEY_SPACE] = ' ', [INPUT_KEY_TAB] = '\t', [INPUT_KEY_ENTER] = '\r',
    [INPUT_KEY_BACKSPACE] = CHAR_DEL,
    /* OK and the jog dial's OK; Cancel and the jog dial's Cancel */
    [INPUT_KEY_F11] = '\r', [INPUT_KEY_F5] = '\r',
    [INPUT_KEY_F4] = CHAR_CTRLC, [INPUT_KEY_F6] = CHAR_CTRLC,
};

static const char shift_map[NUM_KEYS] = {
    [INPUT_KEY_1] = '!', [INPUT_KEY_2] = '"', [INPUT_KEY_3] = '#', [INPUT_KEY_4] = '$',
    [INPUT_KEY_5] = '%', [INPUT_KEY_6] = '&', [INPUT_KEY_7] = '\'', [INPUT_KEY_8] = '(',
    [INPUT_KEY_9] = ')', [INPUT_KEY_0] = '~', [INPUT_KEY_COMMA] = '/',
    [INPUT_KEY_DOT] = '?',
};

static const char fn_map[NUM_KEYS] = {
    [INPUT_KEY_MINUS] = '@', [INPUT_KEY_W] = '^', [INPUT_KEY_E] = '=', [INPUT_KEY_R] = '+',
    [INPUT_KEY_T] = '[', [INPUT_KEY_Y] = ']', [INPUT_KEY_U] = '{', [INPUT_KEY_I] = '}',
    [INPUT_KEY_G] = ';', [INPUT_KEY_H] = ':', [INPUT_KEY_J] = '*', [INPUT_KEY_K] = '\\',
    [INPUT_KEY_L] = '|', [INPUT_KEY_B] = '_', [INPUT_KEY_COMMA] = '<', [INPUT_KEY_DOT] = '>',
};

static const char *drv_kbd_cursorseq(uint16_t code)
{
    switch (code) {
    case INPUT_KEY_UP:
    case INPUT_KEY_F8:
        return "\033[A";
    case INPUT_KEY_DOWN:
    case INPUT_KEY_F7:
        return "\033[B";
    case INPUT_KEY_RIGHT:
        return "\033[C";
    case INPUT_KEY_LEFT:
        return "\033[D";
    default:
        return NULL;
    }
}

static size_t drv_kbd_putseq(char *buf, const char *seq)
{
    size_t len = strlen(seq);

    memcpy(buf, seq, len);

    return len;
}

/* Track a modifier; true when the key is one */
static bool drv_kbd_updatemod(struct drv_kbd_state *state, uint16_t code, bool pressed)
{
    switch (code) {
    case INPUT_KEY_LEFTSHIFT:
        state->shift_l = pressed;
        return true;
    case INPUT_KEY_RIGHTSHIFT:
        state->shift_r = pressed;
        return true;
    case INPUT_KEY_LEFTCTRL:
        state->ctrl = pressed;
        return true;
    case FN_KEY:
        state->fn = pressed;
        return true;
    default:
        return false;
    }
}

size_t drv_kbd_feedtext(struct drv_kbd_state *state, const struct input_event *evt,
                        char *buf)
{
    const char *seq;
    bool shift;
    char c;

    if (evt->type != INPUT_EV_KEY) {
        return 0U;
    }

    if (drv_kbd_updatemod(state, evt->code, evt->value != 0)) {
        return 0U;
    }

    if (evt->value == 0 || evt->code >= NUM_KEYS) {
        return 0U;
    }

    if (state->fn && evt->code == INPUT_KEY_BACKSPACE) {
        return drv_kbd_putseq(buf, "\033[3~");
    }

    seq = drv_kbd_cursorseq(evt->code);
    if (seq != NULL) {
        return drv_kbd_putseq(buf, seq);
    }

    shift = state->shift_l || state->shift_r;

    if (state->fn) {
        c = fn_map[evt->code];
    } else if (shift && shift_map[evt->code] != 0) {
        c = shift_map[evt->code];
    } else {
        c = plain_map[evt->code];
        if (c >= 'a' && c <= 'z') {
            if (state->ctrl) {
                c = (char)(c & 0x1f);
            } else if (shift) {
                c = (char)(c - 'a' + 'A');
            }
        }
    }

    if (c == 0) {
        return 0U;
    }

    buf[0] = c;

    return 1U;
}

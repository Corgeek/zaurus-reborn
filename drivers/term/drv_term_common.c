/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Terminal on a framebuffer display
 *
 * Renders printk(), stdout and log output on the chosen zephyr,display with
 * one of the character framebuffer fonts, scrolling when the screen is full.
 * When the UART console is enabled the text goes to both: this terminal sits
 * in the UART console's output debug hook and lets every character through.
 * drv_term_write() puts text on the display alone, which is what
 * a shell backend uses.
 *
 * The display must expose its framebuffer (RGB565), which is written
 * directly and then cleaned from the data cache for the display's DMA. The
 * text can be rotated against the panel, for panels mounted sideways.
 *
 * Enough of VT100 is understood for the shell's line editing: cursor
 * movement and positioning, erasing, and saving the cursor. The cursor is
 * shown as an inverted cell. Other escape sequences, such as colours, are
 * skipped.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/printk-hooks.h>
#include <zephyr/sys/libc-hooks.h>

#include <zaurus/drivers/term/drv_term_common.h>

#ifdef CONFIG_UART_CONSOLE
#include <zephyr/drivers/console/uart_console.h>
#endif

#ifdef CONFIG_DRV_TERM_LOG_BACKEND
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_backend_std.h>
#include <zephyr/logging/log_output.h>
#endif

#define ROTATION        CONFIG_DRV_TERM_ROTATION
#define TAB_SIZE        (8U)

/* Character cells kept for redrawing under the cursor */
#define MAX_CELLS       (8192U)
#define MAX_PARAMS      (4U)

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xf8U) << 8) | (((g) & 0xfcU) << 3) | ((b) >> 3)))
#define COLOR_FG        RGB565(0xe0U, 0xe0U, 0xe0U)
#define COLOR_BG        RGB565(0x00U, 0x00U, 0x30U)

BUILD_ASSERT(ROTATION == 0 || ROTATION == 90 || ROTATION == 180 || ROTATION == 270,
             "rotation must be 0, 90, 180 or 270");

enum esc_state {
    ESC_NONE,
    ESC_START,
    ESC_CSI,
    ESC_CSI_PRIVATE,
};

struct drv_term_state {
    const struct cfb_font *font;
    uint16_t *fb;
    /* Panel geometry, in framebuffer pixels */
    uint16_t fb_w;
    uint16_t fb_h;
    /* Text screen geometry, after rotation */
    uint16_t scr_w;
    uint16_t scr_h;
    uint16_t cols;
    uint16_t rows;
    /* col == cols means the line is full and the next character wraps */
    uint16_t col;
    uint16_t row;
    uint16_t saved_col;
    uint16_t saved_row;
    /* Where the cursor is drawn, when it is */
    uint16_t cursor_col;
    uint16_t cursor_row;
    uint16_t params[MAX_PARAMS];
    enum esc_state esc;
    uint8_t nparams;
    bool cursor_drawn;
    bool ready;
    uint8_t cells[MAX_CELLS];
};

static struct drv_term_state term;
static struct k_spinlock term_lock;

/* Framebuffer index of text screen pixel (sx, sy) */
static inline size_t drv_term_fbindex(uint16_t sx, uint16_t sy)
{
    uint16_t x;
    uint16_t y;

    if (ROTATION == 90) {
        x = term.fb_w - 1U - sy;
        y = sx;
    } else if (ROTATION == 180) {
        x = term.fb_w - 1U - sx;
        y = term.fb_h - 1U - sy;
    } else if (ROTATION == 270) {
        x = sy;
        y = term.fb_h - 1U - sx;
    } else {
        x = sx;
        y = sy;
    }

    return ((size_t)y * term.fb_w) + x;
}

static void drv_term_flushall(void)
{
    (void)sys_cache_data_flush_range(term.fb, (size_t)term.fb_w * term.fb_h * sizeof(uint16_t));
}

/* Clean one character cell from the data cache, row by framebuffer row */
static void drv_term_flushcell(uint16_t col, uint16_t row)
{
    /* A right-angle rotation keeps the cell a rectangle in the framebuffer */
    size_t a = drv_term_fbindex(col * term.font->width, row * term.font->height);
    size_t b = drv_term_fbindex(((col + 1U) * term.font->width) - 1U,
                                ((row + 1U) * term.font->height) - 1U);

    uint16_t x0 = MIN(a % term.fb_w, b % term.fb_w);
    uint16_t x1 = MAX(a % term.fb_w, b % term.fb_w);
    uint16_t y0 = MIN(a / term.fb_w, b / term.fb_w);
    uint16_t y1 = MAX(a / term.fb_w, b / term.fb_w);

    for (uint16_t y = y0; y <= y1; y++) {
        (void)sys_cache_data_flush_range(&term.fb[((size_t)y * term.fb_w) + x0],
                                         (size_t)(x1 - x0 + 1U) * sizeof(uint16_t));
    }
}

static inline uint8_t *drv_term_cellptr(uint16_t col, uint16_t row)
{
    return &term.cells[((size_t)row * term.cols) + col];
}

/* Draw the cell from the character it holds, inverted for the cursor */
static void drv_term_drawcell(uint16_t col, uint16_t row, bool inverted)
{
    const struct cfb_font *font = term.font;
    const uint8_t bytes_per_col = DIV_ROUND_UP(font->height, 8U);
    const uint8_t *glyph = NULL;
    uint8_t c = *drv_term_cellptr(col, row);
    uint16_t fg = inverted ? COLOR_BG : COLOR_FG;
    uint16_t bg = inverted ? COLOR_FG : COLOR_BG;
    uint16_t x0 = col * font->width;
    uint16_t y0 = row * font->height;

    if (c >= font->first_char && c <= font->last_char && c != ' ') {
        glyph = (const uint8_t *)font->data +
            ((size_t)(c - font->first_char) * font->width * bytes_per_col);
    }

    for (uint8_t gx = 0U; gx < font->width; gx++) {
        for (uint8_t gy = 0U; gy < font->height; gy++) {
            bool on = false;

            if (glyph != NULL) {
                uint8_t byte = glyph[(gx * bytes_per_col) + (gy / 8U)];
                uint8_t bit = ((font->caps & CFB_FONT_MSB_FIRST) != 0U)
                                      ? (7U - (gy % 8U))
                                      : (gy % 8U);

                on = (byte & BIT(bit)) != 0U;
            }

            term.fb[drv_term_fbindex(x0 + gx, y0 + gy)] = on ? fg : bg;
        }
    }

    drv_term_flushcell(col, row);
}

static void drv_term_hidecursor(void)
{
    if (term.cursor_drawn) {
        drv_term_drawcell(term.cursor_col, term.cursor_row, false);
        term.cursor_drawn = false;
    }
}

static void drv_term_showcursor(void)
{
    term.cursor_col = MIN(term.col, term.cols - 1U);
    term.cursor_row = term.row;
    drv_term_drawcell(term.cursor_col, term.cursor_row, true);
    term.cursor_drawn = true;
}

/* Blank cells [first, last] in reading order */
static void drv_term_erase(size_t first, size_t last)
{
    for (size_t i = first; i <= last; i++) {
        term.cells[i] = ' ';
        drv_term_drawcell(i % term.cols, i / term.cols, false);
    }
}

/* Move the text up by one row and clear the last one */
static void drv_term_scroll(void)
{
    const uint16_t lh = term.font->height;
    const uint16_t used_h = term.rows * lh;

    memmove(term.cells, term.cells + term.cols, (size_t)(term.rows - 1U) * term.cols);

    if (ROTATION == 0 || ROTATION == 180) {
        /* Text rows are framebuffer rows: one move of the whole block */
        size_t line = (size_t)term.fb_w * lh;
        size_t keep = (size_t)term.fb_w * (used_h - lh);
        uint16_t *top = term.fb;

        if (ROTATION == 180) {
            top += (size_t)term.fb_w * (term.fb_h - used_h);
            memmove(top + line, top, keep * sizeof(uint16_t));
        } else {
            memmove(top, top + line, keep * sizeof(uint16_t));
        }
    } else {
        /* Text rows are framebuffer columns: shift within each row */
        for (uint16_t y = 0U; y < term.fb_h; y++) {
            uint16_t *r = term.fb + ((size_t)y * term.fb_w);

            if (ROTATION == 90) {
                /* Screen y grows towards framebuffer x = 0 */
                uint16_t *start = r + (term.fb_w - used_h);

                memmove(start + lh, start, (size_t)(used_h - lh) * sizeof(uint16_t));
            } else {
                memmove(r, r + lh, (size_t)(used_h - lh) * sizeof(uint16_t));
            }
        }
    }

    drv_term_flushall();
    drv_term_erase((size_t)(term.rows - 1U) * term.cols, ((size_t)term.rows * term.cols) - 1U);
}

static void drv_term_newline(void)
{
    term.col = 0U;
    if (term.row + 1U < term.rows) {
        term.row++;
    } else {
        drv_term_scroll();
    }
}

static inline uint16_t drv_term_csiparam(uint8_t i, uint16_t def)
{
    return (i < term.nparams && term.params[i] != 0U) ? term.params[i] : def;
}

static void drv_term_csifinal(uint8_t c)
{
    uint16_t n = drv_term_csiparam(0U, 1U);
    size_t here;

    /* A pending wrap does not survive cursor control */
    term.col = MIN(term.col, term.cols - 1U);
    here = ((size_t)term.row * term.cols) + term.col;

    switch (c) {
    case 'A':
        term.row = (n > term.row) ? 0U : (term.row - n);
        break;
    case 'B':
        term.row = MIN(term.row + n, term.rows - 1U);
        break;
    case 'C':
        term.col = MIN(term.col + n, term.cols - 1U);
        break;
    case 'D':
        term.col = (n > term.col) ? 0U : (term.col - n);
        break;
    case 'H':
    case 'f':
        term.row = MIN(drv_term_csiparam(0U, 1U), term.rows) - 1U;
        term.col = MIN(drv_term_csiparam(1U, 1U), term.cols) - 1U;
        break;
    case 'J':
        if (drv_term_csiparam(0U, 0U) == 0U) {
            drv_term_erase(here, ((size_t)term.rows * term.cols) - 1U);
        } else if (drv_term_csiparam(0U, 0U) == 1U) {
            drv_term_erase(0U, here);
        } else {
            drv_term_erase(0U, ((size_t)term.rows * term.cols) - 1U);
        }
        break;
    case 'K': {
        size_t bol = (size_t)term.row * term.cols;

        if (drv_term_csiparam(0U, 0U) == 0U) {
            drv_term_erase(here, bol + term.cols - 1U);
        } else if (drv_term_csiparam(0U, 0U) == 1U) {
            drv_term_erase(bol, here);
        } else {
            drv_term_erase(bol, bol + term.cols - 1U);
        }
        break;
    }
    default:
        /* Colours and modes: nothing to do */
        break;
    }
}

static void drv_term_putlocked(uint8_t c)
{
    switch (term.esc) {
    case ESC_START:
        term.esc = ESC_NONE;
        if (c == '[') {
            term.esc = ESC_CSI;
            term.nparams = 0U;
            memset(term.params, 0, sizeof(term.params));
        } else if (c == '7') {
            term.saved_col = term.col;
            term.saved_row = term.row;
        } else if (c == '8') {
            term.col = term.saved_col;
            term.row = term.saved_row;
        }
        return;
    case ESC_CSI:
    case ESC_CSI_PRIVATE:
        if (c >= '0' && c <= '9') {
            if (term.nparams == 0U) {
                term.nparams = 1U;
            }
            if (term.nparams <= MAX_PARAMS) {
                uint16_t *p = &term.params[term.nparams - 1U];

                *p = (uint16_t)MIN((*p * 10U) + (c - '0'), 9999U);
            }
        } else if (c == ';') {
            term.nparams = (term.nparams == 0U) ? 2U : (term.nparams + 1U);
        } else if (c == '?' || c == '>' || c == '=') {
            term.esc = ESC_CSI_PRIVATE;
        } else if (c >= 0x40U && c <= 0x7eU) {
            /* Private modes (cursor visibility and the like) are ignored */
            if (term.esc == ESC_CSI) {
                drv_term_csifinal(c);
            }
            term.esc = ESC_NONE;
        }
        return;
    default:
        break;
    }

    switch (c) {
    case 0x1bU:
        term.esc = ESC_START;
        break;
    case '\n':
        drv_term_newline();
        break;
    case '\r':
        term.col = 0U;
        break;
    case '\b':
        term.col = MIN(term.col, term.cols - 1U);
        if (term.col > 0U) {
            term.col--;
        }
        break;
    case '\t':
        do {
            drv_term_putlocked(' ');
        } while ((term.col % TAB_SIZE) != 0U && term.col < term.cols);
        break;
    default:
        if (c < 0x20U || c >= 0x7fU) {
            break;
        }
        if (term.col >= term.cols) {
            drv_term_newline();
        }
        *drv_term_cellptr(term.col, term.row) = c;
        drv_term_drawcell(term.col, term.row, false);
        term.col++;
        break;
    }
}

void drv_term_write(const char *buf, size_t len)
{
    k_spinlock_key_t key;

    if (!term.ready) {
        return;
    }

    key = k_spin_lock(&term_lock);
    drv_term_hidecursor();
    for (size_t i = 0U; i < len; i++) {
        drv_term_putlocked((uint8_t)buf[i]);
    }
    drv_term_showcursor();
    k_spin_unlock(&term_lock, key);
}

int drv_term_size(uint16_t *cols, uint16_t *rows)
{
    if (!term.ready) {
        return -ENODEV;
    }

    if (cols != NULL) {
        *cols = term.cols;
    }
    if (rows != NULL) {
        *rows = term.rows;
    }

    return 0;
}

static void drv_term_putchar(uint8_t c)
{
    char ch = (char)c;

    drv_term_write(&ch, 1U);
}

#ifdef CONFIG_UART_CONSOLE
static int drv_term_uarthook(char c)
{
    drv_term_putchar((uint8_t)c);

    /* Not handled: the UART console prints it as well */
    return 0;
}
#else
static int drv_term_stdout(int c)
{
    drv_term_putchar((uint8_t)c);

    return c;
}
#endif

#ifdef CONFIG_DRV_TERM_LOG_BACKEND
static uint8_t log_buf[128];

static int drv_term_logout(uint8_t *data, size_t length, void *ctx)
{
    ARG_UNUSED(ctx);

    drv_term_write((const char *)data, length);

    return (int)length;
}

LOG_OUTPUT_DEFINE(drv_term_logoutput, drv_term_logout, log_buf, sizeof(log_buf));

static void drv_term_logprocess(const struct log_backend *const backend, union log_msg_generic *msg)
{
    /* Colours are escape sequences the terminal would only skip */
    uint32_t flags = log_backend_std_get_flags() & ~LOG_OUTPUT_FLAG_COLORS;

    ARG_UNUSED(backend);

    log_output_msg_process(&drv_term_logoutput, &msg->log, flags);
}

static void drv_term_logpanic(const struct log_backend *const backend)
{
    ARG_UNUSED(backend);

    log_backend_std_panic(&drv_term_logoutput);
}

static void drv_term_logdropped(const struct log_backend *const backend, uint32_t cnt)
{
    ARG_UNUSED(backend);

    log_backend_std_dropped(&drv_term_logoutput, cnt);
}

static const struct log_backend_api drv_term_logapi = {
    .process = drv_term_logprocess,
    .panic = drv_term_logpanic,
    .dropped = IS_ENABLED(CONFIG_LOG_MODE_IMMEDIATE) ? NULL : drv_term_logdropped,
};

LOG_BACKEND_DEFINE(drv_term, drv_term_logapi, true);
#endif /* CONFIG_DRV_TERM_LOG_BACKEND */

static const struct cfb_font *drv_term_pickfont(void)
{
    const struct cfb_font *best = NULL;

    STRUCT_SECTION_FOREACH(cfb_font, font) {
        if ((font->caps & CFB_FONT_MONO_VPACKED) == 0U) {
            continue;
        }
        if (font->height == CONFIG_DRV_TERM_FONT_HEIGHT) {
            return font;
        }
        if (best == NULL || font->height < best->height) {
            best = font;
        }
    }

    return best;
}

static int drv_term_init(void)
{
    const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    struct display_capabilities caps;

    if (!device_is_ready(display)) {
        return -ENODEV;
    }

    display_get_capabilities(display, &caps);
    term.fb = display_get_framebuffer(display);
    term.font = drv_term_pickfont();

    if (term.fb == NULL || caps.current_pixel_format != PIXEL_FORMAT_RGB_565 ||
        term.font == NULL) {
        return -ENOTSUP;
    }

    term.fb_w = caps.x_resolution;
    term.fb_h = caps.y_resolution;
    term.scr_w = (ROTATION % 180 == 0) ? term.fb_w : term.fb_h;
    term.scr_h = (ROTATION % 180 == 0) ? term.fb_h : term.fb_w;
    term.cols = term.scr_w / term.font->width;
    term.rows = MIN(term.scr_h / term.font->height, MAX_CELLS / term.cols);
    memset(term.cells, ' ', sizeof(term.cells));

    /* The part of the screen below the last full row stays background */
    for (size_t i = 0U; i < (size_t)term.fb_w * term.fb_h; i++) {
        term.fb[i] = COLOR_BG;
    }
    drv_term_flushall();
    (void)display_blanking_off(display);

    term.ready = true;

#ifdef CONFIG_UART_CONSOLE
    uart_console_out_debug_hook_install(drv_term_uarthook);
#else
    __printk_hook_install(drv_term_stdout);
    __stdout_hook_install(drv_term_stdout);
#endif

    return 0;
}

SYS_INIT(drv_term_init, POST_KERNEL, CONFIG_DRV_TERM_INIT_PRIORITY);

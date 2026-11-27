/*
 * Copyright (c) 2026 Takayuki Goto <tkg.develop@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zaurus SL-C3x00 application: digital clock on LVGL.
 *
 * The time is drawn with seven-segment digits, hours and minutes large and
 * the seconds small, in the 12-hour format with AM or PM. There is no clock
 * that keeps the time across power off yet, so it starts at AM 9:00 and is
 * set by hand with the buttons under the digits: hours or minutes, up or
 * down. Setting the minutes also clears the seconds, to start on the minute.
 *
 * The buttons are worked from the keyboard: the cursor keys or the jog dial
 * move the focus, Enter, OK or pressing the jog dial presses the button, and
 * holding it repeats. The display is turned to landscape, as the clamshell
 * is normally held.
 *
 * The Zephyr logo sits in the top left corner, on a dark indigo that suits
 * its white lettering, with the revisions of Zephyr and of this
 * application the image was built from (git describe, at configure time).
 * The digits take the light blue of the kite, the buttons Zephyr's purple.
 */

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>
#include <lvgl_input_device.h>
#include <lvgl_zephyr.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/version.h>

/* Time of day at start, in seconds after midnight: AM 9:00 */
#define APP_CLOCK_START_S (9 * 3600)

#define APP_CLOCK_DAY_MS (24 * 3600 * 1000LL)

/* Seven-segment digits, sizes in pixels */
#define APP_CLOCK_SEGMENTS 7

struct app_clock_digitsize {
    int32_t width;
    int32_t height;
    /* Thickness of a segment */
    int32_t thick;
};

static const struct app_clock_digitsize app_clock_large = {.width = 88, .height = 176, .thick = 16};
static const struct app_clock_digitsize app_clock_small = {.width = 48, .height = 88, .thick = 8};

/* Space between the two digits of a field, around a colon, before the seconds */
#define APP_CLOCK_DIGITGAP 16
#define APP_CLOCK_COLONGAP 24
#define APP_CLOCK_COLONWIDTH 32

#define APP_CLOCK_COLOR_ON        0x6ecbff
#define APP_CLOCK_COLOR_OFF       0x261a4a
#define APP_CLOCK_COLOR_BG        0x1a1033
#define APP_CLOCK_COLOR_TEXT      0xc9bfe8
#define APP_CLOCK_COLOR_PRIMARY   0x7929d2
#define APP_CLOCK_COLOR_SECONDARY 0x0070c5

/* app_clock_logo.c */
LV_IMAGE_DECLARE(app_clock_logo);

/* Segments a to g of each figure, as bits 0 to 6 */
static const uint8_t app_clock_figures[10] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f,
};

/* No segment lit, for the leading digit of the hours below 10 */
#define APP_CLOCK_BLANK 0x00

struct app_clock_digit {
    lv_obj_t *segment[APP_CLOCK_SEGMENTS];
    uint8_t shown;
};

enum app_clock_action {
    APP_CLOCK_HOUR_DOWN,
    APP_CLOCK_HOUR_UP,
    APP_CLOCK_MIN_DOWN,
    APP_CLOCK_MIN_UP,
};

static const struct {
    const char *text;
    enum app_clock_action action;
} app_clock_buttons[] = {
    {"Hour -", APP_CLOCK_HOUR_DOWN},
    {"Hour +", APP_CLOCK_HOUR_UP},
    {"Min -", APP_CLOCK_MIN_DOWN},
    {"Min +", APP_CLOCK_MIN_UP},
};

/* Uptime plus this offset, modulo a day, is the time of day */
static int64_t app_clock_offset;

/* Hours (2), minutes (2) and seconds (2), from the left */
static struct app_clock_digit app_clock_digits[6];
static lv_obj_t *app_clock_ampm;
static uint32_t app_clock_shown = UINT32_MAX;

static int64_t app_clock_wrap(int64_t ms)
{
    ms %= APP_CLOCK_DAY_MS;

    return (ms < 0) ? (ms + APP_CLOCK_DAY_MS) : ms;
}

/* Seconds after midnight */
static uint32_t app_clock_now(void)
{
    return (uint32_t)(app_clock_wrap(k_uptime_get() + app_clock_offset) / 1000);
}

/* Make the time of day now be the given seconds after midnight */
static void app_clock_settime(uint32_t seconds)
{
    app_clock_offset = app_clock_wrap((int64_t)seconds * 1000 - k_uptime_get());
}

static void app_clock_adjust(enum app_clock_action action)
{
    uint32_t now = app_clock_now();
    uint32_t hour = now / 3600U;
    uint32_t min = (now / 60U) % 60U;

    switch (action) {
    case APP_CLOCK_HOUR_DOWN:
        app_clock_offset = app_clock_wrap(app_clock_offset - 3600 * 1000LL);
        break;
    case APP_CLOCK_HOUR_UP:
        app_clock_offset = app_clock_wrap(app_clock_offset + 3600 * 1000LL);
        break;
    case APP_CLOCK_MIN_DOWN:
        app_clock_settime((hour * 3600U) + (((min + 59U) % 60U) * 60U));
        break;
    case APP_CLOCK_MIN_UP:
        app_clock_settime((hour * 3600U) + (((min + 1U) % 60U) * 60U));
        break;
    }
}

static lv_obj_t *app_clock_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                               int32_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(APP_CLOCK_COLOR_OFF), 0);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_scrollable(obj, false);

    return obj;
}

/* A digit with its top left corner at x, y, all segments off */
static void app_clock_mkdigit(struct app_clock_digit *digit, lv_obj_t *parent, int32_t x,
                              int32_t y, const struct app_clock_digitsize *size)
{
    int32_t w = size->width;
    int32_t t = size->thick;
    /* Length of a vertical segment */
    int32_t v = (size->height - (3 * t)) / 2;
    /* a, b, c, d, e, f, g: position and size of each segment */
    const int32_t seg[APP_CLOCK_SEGMENTS][4] = {
        {t, 0, w - (2 * t), t},
        {w - t, t, t, v},
        {w - t, (2 * t) + v, t, v},
        {t, (2 * t) + (2 * v), w - (2 * t), t},
        {0, (2 * t) + v, t, v},
        {0, t, t, v},
        {t, t + v, w - (2 * t), t},
    };

    for (size_t i = 0; i < APP_CLOCK_SEGMENTS; i++) {
        digit->segment[i] = app_clock_box(parent, x + seg[i][0], y + seg[i][1], seg[i][2],
                                          seg[i][3], t / 2);
    }

    digit->shown = APP_CLOCK_BLANK;
}

static void app_clock_setdigit(struct app_clock_digit *digit, uint8_t segments)
{
    for (size_t i = 0; i < APP_CLOCK_SEGMENTS; i++) {
        if (((digit->shown ^ segments) & BIT(i)) != 0U) {
            lv_obj_set_style_bg_color(digit->segment[i],
                                      lv_color_hex(((segments & BIT(i)) != 0U)
                                                           ? APP_CLOCK_COLOR_ON
                                                           : APP_CLOCK_COLOR_OFF),
                                      0);
        }
    }

    digit->shown = segments;
}

/* A colon of two square dots, centered on a field of the given height */
static void app_clock_mkcolon(lv_obj_t *parent, int32_t x, int32_t height, int32_t dot)
{
    lv_obj_t *upper = app_clock_box(parent, x, (height / 3) - (dot / 2), dot, dot, dot / 4);
    lv_obj_t *lower = app_clock_box(parent, x, ((2 * height) / 3) - (dot / 2), dot, dot, dot / 4);

    lv_obj_set_style_bg_color(upper, lv_color_hex(APP_CLOCK_COLOR_ON), 0);
    lv_obj_set_style_bg_color(lower, lv_color_hex(APP_CLOCK_COLOR_ON), 0);
}

static void app_clock_update(void)
{
    uint32_t now = app_clock_now();
    uint32_t hour = now / 3600U;
    uint32_t hour12 = ((hour + 11U) % 12U) + 1U;
    uint32_t min = (now / 60U) % 60U;
    uint32_t sec = now % 60U;

    if (now == app_clock_shown) {
        return;
    }

    app_clock_shown = now;

    app_clock_setdigit(&app_clock_digits[0],
                       (hour12 >= 10U) ? app_clock_figures[hour12 / 10U] : APP_CLOCK_BLANK);
    app_clock_setdigit(&app_clock_digits[1], app_clock_figures[hour12 % 10U]);
    app_clock_setdigit(&app_clock_digits[2], app_clock_figures[min / 10U]);
    app_clock_setdigit(&app_clock_digits[3], app_clock_figures[min % 10U]);
    app_clock_setdigit(&app_clock_digits[4], app_clock_figures[sec / 10U]);
    app_clock_setdigit(&app_clock_digits[5], app_clock_figures[sec % 10U]);
    lv_label_set_text_static(app_clock_ampm, (hour < 12U) ? "AM" : "PM");
}

static void app_clock_tick(lv_timer_t *timer)
{
    ARG_UNUSED(timer);

    app_clock_update();
}

static void app_clock_pressed(lv_event_t *e)
{
    enum app_clock_action action = (enum app_clock_action)(uintptr_t)lv_event_get_user_data(e);

    app_clock_adjust(action);
    app_clock_update();
}

/* Hours and minutes large, then AM/PM above the small seconds */
static void app_clock_mkface(lv_obj_t *screen)
{
    const struct app_clock_digitsize *big = &app_clock_large;
    const struct app_clock_digitsize *small = &app_clock_small;
    int32_t x = 0;
    lv_obj_t *face = lv_obj_create(screen);

    lv_obj_remove_style_all(face);
    lv_obj_set_clickable(face, false);
    lv_obj_set_scrollable(face, false);

    for (size_t i = 0; i < 4; i++) {
        app_clock_mkdigit(&app_clock_digits[i], face, x, 0, big);
        x += big->width + APP_CLOCK_DIGITGAP;

        if (i == 1) {
            x += APP_CLOCK_COLONGAP - APP_CLOCK_DIGITGAP;
            app_clock_mkcolon(face, x, big->height, big->thick);
            x += APP_CLOCK_COLONWIDTH + APP_CLOCK_COLONGAP;
        }
    }

    x += APP_CLOCK_COLONGAP - APP_CLOCK_DIGITGAP;

    app_clock_ampm = lv_label_create(face);
    lv_obj_set_style_text_font(app_clock_ampm, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(app_clock_ampm, lv_color_hex(APP_CLOCK_COLOR_ON), 0);
    lv_obj_set_pos(app_clock_ampm, x, 0);

    for (size_t i = 4; i < 6; i++) {
        app_clock_mkdigit(&app_clock_digits[i], face, x, big->height - small->height, small);
        x += small->width + (small->thick);
    }

    lv_obj_set_size(face, x - small->thick, big->height);
    lv_obj_align(face, LV_ALIGN_TOP_MID, 0, 120);
}

static void app_clock_mkbuttons(lv_obj_t *screen)
{
    lv_obj_t *row = lv_obj_create(screen);

    lv_obj_remove_style_all(row);
    lv_obj_set_clickable(row, false);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_size(row, LV_PCT(100), 72);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -56);

    for (size_t i = 0; i < ARRAY_SIZE(app_clock_buttons); i++) {
        lv_obj_t *button = lv_button_create(row);
        lv_obj_t *label = lv_label_create(button);

        lv_obj_set_size(button, 132, 56);
        lv_label_set_text_static(label, app_clock_buttons[i].text);
        lv_obj_center(label);

        /* Pressed once, then again and again while held */
        lv_obj_add_event_cb(button, app_clock_pressed, LV_EVENT_PRESSED,
                            (void *)(uintptr_t)app_clock_buttons[i].action);
        lv_obj_add_event_cb(button, app_clock_pressed, LV_EVENT_LONG_PRESSED_REPEAT,
                            (void *)(uintptr_t)app_clock_buttons[i].action);
    }
}

/* The logo, and beside it the revisions this image was built from */
static void app_clock_mkheader(lv_obj_t *screen)
{
    lv_obj_t *logo = lv_image_create(screen);
    lv_obj_t *zephyr = lv_label_create(screen);
    lv_obj_t *app = lv_label_create(screen);

    lv_image_set_src(logo, &app_clock_logo);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 16, 12);

    lv_label_set_text_static(zephyr, "Zephyr " STRINGIFY(BUILD_VERSION));
    lv_obj_set_style_text_color(zephyr, lv_color_hex(APP_CLOCK_COLOR_TEXT), 0);
    lv_obj_align_to(zephyr, logo, LV_ALIGN_OUT_RIGHT_MID, 24, -14);

    lv_label_set_text_static(app, "zaurus " APP_CLOCK_REVISION);
    lv_obj_set_style_text_color(app, lv_color_hex(APP_CLOCK_COLOR_TEXT), 0);
    lv_obj_align_to(app, logo, LV_ALIGN_OUT_RIGHT_MID, 24, 14);
}

static void app_clock_mkscreen(void)
{
    lv_obj_t *screen = lv_screen_active();
    lv_obj_t *hint = lv_label_create(screen);

    lv_obj_set_scrollable(screen, false);
    lv_obj_set_style_bg_color(screen, lv_color_hex(APP_CLOCK_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    app_clock_mkheader(screen);
    app_clock_mkface(screen);
    app_clock_mkbuttons(screen);

    lv_label_set_text_static(hint, "Arrows, jog: choose    Enter, OK: set, hold to repeat");
    lv_obj_set_style_text_color(hint, lv_color_hex(APP_CLOCK_COLOR_TEXT), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -16);
}

int main(void)
{
    const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    const struct device *keypad = DEVICE_DT_GET(DT_NODELABEL(app_clock_keypad));
    lv_group_t *group;
    int ret;

    printk("Zaurus LVGL clock on %s\n", CONFIG_BOARD_TARGET);

    if (!device_is_ready(display)) {
        printk("Display not ready\n");
        return 0;
    }

    /* Landscape, the top of the picture on the panel's right edge */
    ret = display_set_orientation(display, DISPLAY_ORIENTATION_ROTATED_90);
    if (ret != 0) {
        printk("Display orientation failed (%d)\n", ret);
    }

    /* LVGL reads the resolution at init, so after turning the display */
    ret = lvgl_init();
    if (ret != 0) {
        printk("LVGL init failed (%d)\n", ret);
        return 0;
    }

    /* The default theme, dark, in the colors of the logo */
    lv_display_set_theme(lv_display_get_default(),
                         lv_theme_default_init(lv_display_get_default(),
                                               lv_color_hex(APP_CLOCK_COLOR_PRIMARY),
                                               lv_color_hex(APP_CLOCK_COLOR_SECONDARY), true,
                                               LV_FONT_DEFAULT));

    /* The buttons join the default group, which the keypad moves through */
    group = lv_group_create();
    lv_group_set_default(group);
    lv_indev_set_group(lvgl_input_get_indev(keypad), group);

    app_clock_settime(APP_CLOCK_START_S);
    app_clock_mkscreen();
    app_clock_update();
    lv_timer_create(app_clock_tick, 100, NULL);

    lv_timer_handler();
    display_blanking_off(display);

    while (true) {
        uint32_t sleep = lv_timer_handler();

        k_msleep(MIN(sleep, 100U));
    }

    return 0;
}

/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <stdio.h>

#include <lvgl.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/usb.h>

#include "widgets/util.h"

LV_IMG_DECLARE(custom_portrait);

/*
 * Fanciful status screen for the peripheral (right) half. Battery +
 * connection state reuse the same canvas drawing primitives as the central
 * screen (draw_battery/rotate_canvas from nice_view/widgets/util.c); the
 * decorative side is a custom bitmap (art_right.c), placed directly like
 * stock nice_view's balloon/mountain images (no canvas/rotation needed for
 * plain lv_img content). The art frame sizes itself from the image.
 */

static lv_obj_t *canvas_top;
static uint8_t cbuf_top[CANVAS_BUF_SIZE];

static struct status_state screen_state;
/* struct battery_status_state comes from nice_view/widgets/util.h */

static void draw_top(void) {
    if (canvas_top == NULL) {
        return;
    }

    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_label_dsc_t label_dsc_battery;
    init_label_dsc(&label_dsc_battery, LVGL_FOREGROUND, &lv_font_montserrat_12, LV_TEXT_ALIGN_LEFT);

    lv_canvas_fill_bg(canvas_top, LVGL_BACKGROUND, LV_OPA_COVER);

    draw_battery(canvas_top, &screen_state);

    char battery_text[6] = {};
    snprintf(battery_text, sizeof(battery_text), "%u%%", screen_state.battery);
    canvas_draw_text(canvas_top, 0, 15, 40, &label_dsc_battery, battery_text);

    canvas_draw_text(canvas_top, 0, 0, CANVAS_SIZE, &label_dsc,
                     screen_state.connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    rotate_canvas(canvas_top);
}

static void battery_status_update_cb(struct battery_status_state st) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    screen_state.charging = st.usb_present;
#endif
    screen_state.battery = st.level;

    draw_top();
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(right_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(right_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(right_battery_status, zmk_usb_conn_state_changed);
#endif

struct peripheral_conn_state {
    bool connected;
};

static void peripheral_status_update_cb(struct peripheral_conn_state st) {
    screen_state.connected = st.connected;

    draw_top();
}

static struct peripheral_conn_state peripheral_status_get_state(const zmk_event_t *eh) {
    return (struct peripheral_conn_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

ZMK_DISPLAY_WIDGET_LISTENER(right_peripheral_status, struct peripheral_conn_state,
                            peripheral_status_update_cb, peripheral_status_get_state)
ZMK_SUBSCRIPTION(right_peripheral_status, zmk_split_peripheral_status_changed);

/*
 * The battery/connection canvas only draws into roughly the first half of
 * its 68x68 box - the rest renders as blank space bordering the art region,
 * confirmed safe against real hardware. The canvas itself stays at its
 * original position/size; the art frame is sized to match custom_portrait's
 * own compiled-in dimensions (see art_right.c) so the two can't drift out of
 * sync, and is drawn after (on top of) canvas_top so its border remains
 * visible over the canvas's unused portion.
 */
lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, 160, 68);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);

    canvas_top = lv_canvas_create(screen);
    lv_obj_align(canvas_top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(canvas_top, cbuf_top, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    lv_obj_t *art_frame = lv_obj_create(screen);
    lv_obj_set_pos(art_frame, 0, 0);
    lv_obj_set_size(art_frame, custom_portrait.header.w, custom_portrait.header.h);
    lv_obj_set_style_pad_all(art_frame, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(art_frame, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(art_frame, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(art_frame, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(art_frame, 0, LV_PART_MAIN);

    lv_obj_t *art = lv_img_create(art_frame);
    lv_image_set_src(art, &custom_portrait);
    lv_obj_align(art, LV_ALIGN_TOP_LEFT, 0, 0);

    right_battery_status_init();
    right_peripheral_status_init();

    return screen;
}

/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <stdio.h>

#include <lvgl.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/usb.h>
#include <zmk/wpm.h>

#include "widgets/util.h"

LV_IMG_DECLARE(rook_icon_top);
LV_IMG_DECLARE(rook_icon_bottom);

/*
 * Detailed, information-dense status screen for the central (left) half.
 * Reuses the drawing primitives from nice_view/widgets/util.c (rotate_canvas,
 * draw_battery, canvas_draw_*, init_*_dsc) and the same struct status_state /
 * three-canvas 160x68 tiling stock nice_view uses, but with its own layout:
 *   - top canvas:    battery %, output transport icon, WPM value + history bars
 *   - middle canvas: 5 BLE profiles as small rings in a compact "Olympic
 *                    rings" layout (3 over 2, not touching) - solid = connected,
 *                    dotted = bonded, none = unpaired, active profile filled
 *                    in - plus the active layer name below the rings
 *   - bottom canvas: fully reserved for dedicated art (see draw_bottom_art)
 */

static lv_obj_t *canvas_top;
static lv_obj_t *canvas_mid;
static lv_obj_t *canvas_bot;

static uint8_t cbuf_top[CANVAS_BUF_SIZE];
static uint8_t cbuf_mid[CANVAS_BUF_SIZE];
static uint8_t cbuf_bot[CANVAS_BUF_SIZE];

static struct status_state screen_state;

struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    int active_profile_index;
    bool active_profile_connected;
    bool active_profile_bonded;
    bool profiles_connected[NICEVIEW_PROFILE_COUNT];
    bool profiles_bonded[NICEVIEW_PROFILE_COUNT];
};

struct layer_status_state {
    uint8_t index;
    const char *label;
};

struct wpm_status_state {
    uint8_t wpm;
};

static void draw_top(void) {
    if (canvas_top == NULL) {
        return;
    }

    lv_draw_label_dsc_t label_dsc_output;
    init_label_dsc(&label_dsc_output, LVGL_FOREGROUND, &lv_font_montserrat_18, LV_TEXT_ALIGN_RIGHT);
    lv_draw_label_dsc_t label_dsc_battery;
    init_label_dsc(&label_dsc_battery, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    lv_draw_label_dsc_t label_dsc_wpm_title;
    init_label_dsc(&label_dsc_wpm_title, LVGL_FOREGROUND, &lv_font_unscii_8, LV_TEXT_ALIGN_LEFT);
    lv_draw_label_dsc_t label_dsc_wpm_value;
    init_label_dsc(&label_dsc_wpm_value, LVGL_FOREGROUND, &lv_font_unscii_8, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_white_dsc;
    init_rect_dsc(&rect_white_dsc, LVGL_FOREGROUND);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    lv_canvas_fill_bg(canvas_top, LVGL_BACKGROUND, LV_OPA_COVER);

    draw_battery(canvas_top, &screen_state);

    char battery_text[6] = {};
    snprintf(battery_text, sizeof(battery_text), "%u%%", screen_state.battery);
    canvas_draw_text(canvas_top, 0, 15, 40, &label_dsc_battery, battery_text);

    char output_text[10] = {};
    switch (screen_state.selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        strcat(output_text, LV_SYMBOL_USB);
        break;
    case ZMK_TRANSPORT_BLE:
        if (screen_state.active_profile_bonded) {
            strcat(output_text,
                   screen_state.active_profile_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);
        } else {
            strcat(output_text, LV_SYMBOL_SETTINGS);
        }
        break;
    default:
        break;
    }
    canvas_draw_text(canvas_top, 0, 0, CANVAS_SIZE, &label_dsc_output, output_text);

    canvas_draw_rect(canvas_top, 0, 32, CANVAS_SIZE, 36, &rect_white_dsc);
    canvas_draw_rect(canvas_top, 1, 33, CANVAS_SIZE - 2, 34, &rect_black_dsc);
    canvas_draw_text(canvas_top, 4, 35, 28, &label_dsc_wpm_title, "WPM");

    char wpm_text[6] = {};
    snprintf(wpm_text, sizeof(wpm_text), "%d", screen_state.wpm[9]);
    canvas_draw_text(canvas_top, 32, 35, CANVAS_SIZE - 36, &label_dsc_wpm_value, wpm_text);

    int max = 0;
    int min = 256;
    for (int i = 0; i < 10; i++) {
        if (screen_state.wpm[i] > max) {
            max = screen_state.wpm[i];
        }
        if (screen_state.wpm[i] < min) {
            min = screen_state.wpm[i];
        }
    }
    int range = max - min;
    if (range == 0) {
        range = 1;
    }

    for (int i = 0; i < 10; i++) {
        int bar_h = ((screen_state.wpm[i] - min) * 18) / range;
        if (bar_h < 1) {
            bar_h = 1;
        }
        canvas_draw_rect(canvas_top, 3 + i * 6, 65 - bar_h, 4, bar_h, &rect_white_dsc);
    }

    rotate_canvas(canvas_top);
}

#define PROFILE_RING_R 8

static void draw_mid(void) {
    if (canvas_mid == NULL) {
        return;
    }

    lv_draw_arc_dsc_t arc_dsc;
    init_arc_dsc(&arc_dsc, LVGL_FOREGROUND, 2);
    lv_draw_arc_dsc_t arc_dsc_filled;
    init_arc_dsc(&arc_dsc_filled, LVGL_FOREGROUND, 5);
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_unscii_8, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t label_dsc_active;
    init_label_dsc(&label_dsc_active, LVGL_BACKGROUND, &lv_font_unscii_8, LV_TEXT_ALIGN_CENTER);
    lv_draw_label_dsc_t label_dsc_layer;
    init_label_dsc(&label_dsc_layer, LVGL_FOREGROUND, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(canvas_mid, LVGL_BACKGROUND, LV_OPA_COVER);

    /* Compact "Olympic rings" layout: 1-3 spread across the full width on
     * top, 4-5 below in the gaps between them, close enough vertically to
     * keep the section short while still leaving a gap so rings never touch. */
    static const int circle_offsets[NICEVIEW_PROFILE_COUNT][2] = {
        {10, 9}, {34, 9}, {58, 9}, {22, 23}, {46, 23},
    };

    for (int i = 0; i < NICEVIEW_PROFILE_COUNT; i++) {
        bool active = i == screen_state.active_profile_index;

        if (screen_state.profiles_connected[i]) {
            /* Connected: solid ring. */
            canvas_draw_arc(canvas_mid, circle_offsets[i][0], circle_offsets[i][1],
                            PROFILE_RING_R, 0, 360, &arc_dsc);
        } else if (screen_state.profiles_bonded[i]) {
            /* Bonded but not connected: dotted/segmented ring. */
            const int segments = 6;
            const int gap = 24;
            for (int j = 0; j < segments; ++j) {
                canvas_draw_arc(canvas_mid, circle_offsets[i][0], circle_offsets[i][1],
                                PROFILE_RING_R, 360. / segments * j + gap / 2.0,
                                360. / segments * (j + 1) - gap / 2.0, &arc_dsc);
            }
        }
        /* Unpaired: no ring at all. */

        if (active) {
            canvas_draw_arc(canvas_mid, circle_offsets[i][0], circle_offsets[i][1],
                            PROFILE_RING_R - 3, 0, 359, &arc_dsc_filled);
        }

        char label[2];
        snprintf(label, sizeof(label), "%d", i + 1);
        canvas_draw_text(canvas_mid, circle_offsets[i][0] - 6, circle_offsets[i][1] - 4, 12,
                         (active ? &label_dsc_active : &label_dsc), label);
    }

    char layer_text[16] = {};
    if (screen_state.layer_label == NULL || strlen(screen_state.layer_label) == 0) {
        snprintf(layer_text, sizeof(layer_text), "LAYER %d", screen_state.layer_index);
    } else {
        snprintf(layer_text, sizeof(layer_text), "%s", screen_state.layer_label);
    }
    /* Bottom row of rings ends at y=31 (23 + PROFILE_RING_R); keep >=5px clear. */
    canvas_draw_text(canvas_mid, 0, 36, CANVAS_SIZE, &label_dsc_layer, layer_text);

    /* rook_icon_top: see the art-region comment on draw_bottom_art below -
     * this canvas isn't placement-clipped, but the layer text (y=36,
     * line_height=16) ends at y=51, so y=52 is the first free row. */
    lv_draw_image_dsc_t img_dsc;
    lv_draw_image_dsc_init(&img_dsc);
    canvas_draw_img(canvas_mid, 0, 52, &rook_icon_top, &img_dsc);

    rotate_canvas(canvas_mid);
}

/*
 * Reserved for dedicated art. The art is drawn directly into the canvas via
 * canvas_draw_img, same as the charging-bolt icon in draw_battery - no
 * pre-rotated art is needed here, unlike the plain lv_img on the right
 * screen.
 *
 * This canvas is placed at LV_ALIGN_TOP_LEFT with dx=-44 (matching stock
 * nice_view's own bottom canvas), so only 24 of its 68 columns land on the
 * physical framebuffer. rotate_canvas() rotates the buffer before that
 * placement clip applies, which swaps axes: it's the *y* coordinate passed
 * to canvas_draw_* here that ends up clipped, not x - stock's own layer text
 * only ever used a single line near y=5 for exactly this reason. The
 * visible band here is x:[0,68) (always fully visible) by y:[0,24).
 *
 * That 24px band, plus the free space below canvas_mid's layer text (y=52
 * to 68, 16px - see draw_mid), forms one continuous 68x40 art strip on the
 * physical display even though it has to be drawn as two separate images
 * (art_left.c's rook_icon_top/rook_icon_bottom) into two different canvases.
 * Static content only - drawn once at screen creation, not redrawn on state
 * changes.
 */
static void draw_bottom_art(lv_obj_t *canvas) {
    lv_draw_image_dsc_t img_dsc;
    lv_draw_image_dsc_init(&img_dsc);
    canvas_draw_img(canvas, 0, 0, &rook_icon_bottom, &img_dsc);
}

static void draw_bottom(void) {
    if (canvas_bot == NULL) {
        return;
    }

    lv_canvas_fill_bg(canvas_bot, LVGL_BACKGROUND, LV_OPA_COVER);

    draw_bottom_art(canvas_bot);

    rotate_canvas(canvas_bot);
}

static void battery_status_update_cb(struct battery_status_state st) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    screen_state.charging = st.usb_present;
#endif
    screen_state.battery = st.level;

    draw_top();
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(left_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)

ZMK_SUBSCRIPTION(left_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(left_battery_status, zmk_usb_conn_state_changed);
#endif

static void output_status_update_cb(struct output_status_state st) {
    screen_state.selected_endpoint = st.selected_endpoint;
    screen_state.active_profile_index = st.active_profile_index;
    screen_state.active_profile_connected = st.active_profile_connected;
    screen_state.active_profile_bonded = st.active_profile_bonded;
    for (int i = 0; i < NICEVIEW_PROFILE_COUNT; i++) {
        screen_state.profiles_connected[i] = st.profiles_connected[i];
        screen_state.profiles_bonded[i] = st.profiles_bonded[i];
    }

    draw_top();
    draw_mid();
}

static struct output_status_state output_status_get_state(const zmk_event_t *eh) {
    struct output_status_state st = {
        .selected_endpoint = zmk_endpoint_get_selected(),
        .active_profile_index = zmk_ble_active_profile_index(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
    for (int i = 0; i < MIN(NICEVIEW_PROFILE_COUNT, ZMK_BLE_PROFILE_COUNT); i++) {
        st.profiles_connected[i] = zmk_ble_profile_is_connected(i);
        st.profiles_bonded[i] = !zmk_ble_profile_is_open(i);
    }
    return st;
}

ZMK_DISPLAY_WIDGET_LISTENER(left_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)

ZMK_SUBSCRIPTION(left_output_status, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(left_output_status, zmk_usb_conn_state_changed);
#endif
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(left_output_status, zmk_ble_active_profile_changed);
#endif

static void layer_status_update_cb(struct layer_status_state st) {
    screen_state.layer_index = st.index;
    screen_state.layer_label = st.label;

    draw_mid();
}

static struct layer_status_state layer_status_get_state(const zmk_event_t *eh) {
    uint8_t index = zmk_keymap_highest_layer_active();
    return (struct layer_status_state){
        .index = index,
        .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index)),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(left_layer_status, struct layer_status_state, layer_status_update_cb,
                            layer_status_get_state)

ZMK_SUBSCRIPTION(left_layer_status, zmk_layer_state_changed);

static void wpm_status_update_cb(struct wpm_status_state st) {
    for (int i = 0; i < 9; i++) {
        screen_state.wpm[i] = screen_state.wpm[i + 1];
    }
    screen_state.wpm[9] = st.wpm;

    draw_top();
}

static struct wpm_status_state wpm_status_get_state(const zmk_event_t *eh) {
    return (struct wpm_status_state){.wpm = zmk_wpm_get_state()};
}

ZMK_DISPLAY_WIDGET_LISTENER(left_wpm_status, struct wpm_status_state, wpm_status_update_cb,
                            wpm_status_get_state)
ZMK_SUBSCRIPTION(left_wpm_status, zmk_wpm_state_changed);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, 160, 68);

    canvas_top = lv_canvas_create(screen);
    lv_obj_align(canvas_top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(canvas_top, cbuf_top, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    canvas_mid = lv_canvas_create(screen);
    lv_obj_align(canvas_mid, LV_ALIGN_TOP_LEFT, 24, 0);
    lv_canvas_set_buffer(canvas_mid, cbuf_mid, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    canvas_bot = lv_canvas_create(screen);
    lv_obj_align(canvas_bot, LV_ALIGN_TOP_LEFT, -44, 0);
    lv_canvas_set_buffer(canvas_bot, cbuf_bot, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    draw_bottom();

    left_battery_status_init();
    left_output_status_init();
    left_layer_status_init();
    left_wpm_status_init();

    return screen;
}

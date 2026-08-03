/*
 * Copyright (c) 2026 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include <stdio.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/display/widgets/layer_status.h>
#include <zmk/display/widgets/wpm_status.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

static void style_fill(lv_obj_t *obj, lv_color_t color, lv_coord_t border) {
    lv_obj_set_style_border_width(obj, border, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 1, LV_PART_MAIN);
}

static void rotate_label(lv_obj_t *label, int32_t angle) {
    lv_obj_set_style_transform_angle(label, angle, LV_PART_MAIN);
}

static lv_obj_t *create_section(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                                lv_coord_t h, const char *title) {
    lv_obj_t *section = lv_obj_create(parent);
    lv_obj_set_pos(section, x, y);
    lv_obj_set_size(section, w, h);
    style_fill(section, lv_color_white(), 1);

    lv_obj_t *label = lv_label_create(section);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    rotate_label(label, 900);

    return section;
}

static void add_left_art(lv_obj_t *parent) {
    static const lv_coord_t bar_offsets[] = {2, 5, 8, 3, 6, 9, 4, 7};
    for (int i = 0; i < (int)(sizeof(bar_offsets) / sizeof(bar_offsets[0])); i++) {
        lv_obj_t *bar = lv_obj_create(parent);
        lv_obj_set_size(bar, 3, 6);
        lv_obj_set_pos(bar, bar_offsets[i], 4 + (i * 8));
        style_fill(bar, lv_color_black(), 0);
    }
}

#if IS_ENABLED(CONFIG_ZMK_WIDGET_WPM_STATUS)
static struct zmk_widget_wpm_status wpm_status_widget;
#endif

#if IS_ENABLED(CONFIG_ZMK_WIDGET_LAYER_STATUS)
static struct zmk_widget_layer_status layer_status_widget;
#endif

static lv_obj_t *connection_value_label;
static lv_obj_t *battery_value_label;
static lv_obj_t *profile_value_label;

struct connection_status_state {
    enum zmk_transport transport;
    bool connected;
};

static void connection_status_update_cb(struct connection_status_state state) {
    if (connection_value_label == NULL) {
        return;
    }

    if (state.transport == ZMK_TRANSPORT_USB && state.connected) {
        lv_label_set_text(connection_value_label, "USB");
    } else if (state.transport == ZMK_TRANSPORT_BLE && state.connected) {
        lv_label_set_text(connection_value_label, "WLS");
    } else if (state.transport == ZMK_TRANSPORT_BLE) {
        lv_label_set_text(connection_value_label, "WLS?");
    } else {
        lv_label_set_text(connection_value_label, "OFF");
    }

    lv_obj_set_style_text_color(connection_value_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(connection_value_label, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static struct connection_status_state connection_status_get_state(const zmk_event_t *eh) {
    struct zmk_endpoint_instance endpoint = zmk_endpoint_get_selected();
    return (struct connection_status_state){
        .transport = endpoint.transport,
        .connected = endpoint.transport != ZMK_TRANSPORT_NONE,
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_connection_status, struct connection_status_state,
                            connection_status_update_cb, connection_status_get_state)

ZMK_SUBSCRIPTION(widget_connection_status, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(widget_connection_status, zmk_ble_active_profile_changed);

struct battery_text_state {
    uint8_t level;
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    bool usb_present;
#endif
};

static void battery_text_update_cb(struct battery_text_state state) {
    if (battery_value_label == NULL) {
        return;
    }

    char text[8] = {0};
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    snprintf(text, sizeof(text), "%u%%%s", state.level, state.usb_present ? "+" : "");
#else
    snprintf(text, sizeof(text), "%u%%", state.level);
#endif

    lv_label_set_text(battery_value_label, text);
    lv_obj_set_style_text_color(battery_value_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(battery_value_label, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static struct battery_text_state battery_text_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    return (struct battery_text_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_text, struct battery_text_state, battery_text_update_cb,
                            battery_text_get_state)

ZMK_SUBSCRIPTION(widget_battery_text, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_text, zmk_usb_conn_state_changed);
#endif

struct profile_status_state {
    enum zmk_transport transport;
    uint8_t index;
    bool connected;
};

static void profile_status_update_cb(struct profile_status_state state) {
    if (profile_value_label == NULL) {
        return;
    }

    char text[10] = {0};

    switch (state.transport) {
    case ZMK_TRANSPORT_USB:
        snprintf(text, sizeof(text), "USB");
        break;
    case ZMK_TRANSPORT_BLE:
        snprintf(text, sizeof(text), "P%u%s", state.index + 1, state.connected ? "*" : "");
        break;
    case ZMK_TRANSPORT_NONE:
    default:
        snprintf(text, sizeof(text), "OFF");
        break;
    }

    lv_label_set_text(profile_value_label, text);
    lv_obj_set_style_text_color(profile_value_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(profile_value_label, LV_ALIGN_BOTTOM_MID, 0, 0);
}

static struct profile_status_state profile_status_get_state(const zmk_event_t *eh) {
    struct zmk_endpoint_instance endpoint = zmk_endpoint_get_selected();

    return (struct profile_status_state){
        .transport = endpoint.transport,
        .index = zmk_ble_active_profile_index(),
        .connected = zmk_ble_active_profile_is_connected(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_profile_status, struct profile_status_state, profile_status_update_cb,
                            profile_status_get_state)

ZMK_SUBSCRIPTION(widget_profile_status, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(widget_profile_status, zmk_endpoint_changed);

lv_obj_t *zmk_display_status_screen(void) {
    const lv_coord_t screen_w = 68;
    const lv_coord_t screen_h = 160;

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, screen_w, screen_h);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);
    style_fill(screen, lv_color_white(), 1);

    lv_obj_t *left_art = lv_obj_create(screen);
    lv_obj_set_pos(left_art, 0, 0);
    lv_obj_set_size(left_art, 12, 160);
    style_fill(left_art, lv_color_white(), 1);
    add_left_art(left_art);

    lv_obj_t *connection_block = create_section(screen, 14, 0, 52, 28, "CONN");
    lv_obj_t *battery_block = create_section(screen, 14, 30, 52, 28, "BATT");
    lv_obj_t *profile_block = create_section(screen, 14, 60, 52, 28, "PROF");
    lv_obj_t *wpm_block = create_section(screen, 14, 90, 52, 28, "WPM");
    lv_obj_t *layer_block = create_section(screen, 14, 120, 52, 28, "LAYER");

    connection_value_label = lv_label_create(connection_block);
    lv_obj_set_style_text_font(connection_value_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(connection_value_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(connection_value_label, LV_ALIGN_BOTTOM_MID, 0, 0);
    widget_connection_status_init();

    battery_value_label = lv_label_create(battery_block);
    lv_obj_set_style_text_font(battery_value_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(battery_value_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_align(battery_value_label, LV_ALIGN_BOTTOM_MID, 0, 0);
    widget_battery_text_init();

#if IS_ENABLED(CONFIG_ZMK_WIDGET_WPM_STATUS)
    zmk_widget_wpm_status_init(&wpm_status_widget, wpm_block);
    lv_obj_set_style_text_color(zmk_widget_wpm_status_obj(&wpm_status_widget), lv_color_black(),
                                LV_PART_MAIN);
    lv_obj_align(zmk_widget_wpm_status_obj(&wpm_status_widget), LV_ALIGN_BOTTOM_MID, 0, 0);
#endif

#if IS_ENABLED(CONFIG_ZMK_WIDGET_LAYER_STATUS)
    zmk_widget_layer_status_init(&layer_status_widget, layer_block);
    lv_obj_set_style_text_color(zmk_widget_layer_status_obj(&layer_status_widget), lv_color_black(),
                                LV_PART_MAIN);
    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget), LV_ALIGN_BOTTOM_MID, 0, 0);
#endif

    profile_value_label = lv_label_create(profile_block);
    lv_label_set_text(profile_value_label, "P?");
    lv_obj_set_style_text_color(profile_value_label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_font(profile_value_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(profile_value_label, LV_ALIGN_BOTTOM_MID, 0, 0);
    widget_profile_status_init();

    return screen;
}
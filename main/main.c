#include <inttypes.h>
#include <stdio.h>
#include "round_display.h"
#include "esp_lvgl_port.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static lv_obj_t *pages[3], *coordinates, *cursor, *ball, *stats;
static unsigned page;
static uint32_t refreshes, render_ms, rendered_pixels;
static lv_disp_t *display;

/* Runs in the LVGL task. Counts LVGL refresh cycles, not physical panel FPS. */
static void monitor_render(lv_disp_drv_t *driver, uint32_t elapsed, uint32_t pixels)
{
    (void)driver;
    refreshes++;
    render_ms += elapsed;
    rendered_pixels += pixels;
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_color(obj, lv_color_white(), 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *rectangle(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static void touch_event(lv_event_t *event)
{
    lv_indev_t *input = lv_indev_get_act();
    if (!input) return;
    lv_point_t point;
    lv_indev_get_point(input, &point);
    if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
        lv_obj_add_flag(cursor, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(coordinates, "Released: %d,%d", point.x, point.y);
    } else {
        lv_obj_clear_flag(cursor, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(cursor, point.x - 4, point.y - 4);
        lv_label_set_text_fmt(coordinates, "Touch: %d,%d", point.x, point.y);
    }
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    /* BOOT is active low. Require 40 ms stable before switching once. */
    static int raw_previous = 1, stable = 1;
    static int64_t changed_at;
    int raw = gpio_get_level(GPIO_NUM_9);
    int64_t now = esp_timer_get_time();
    if (raw != raw_previous) { raw_previous = raw; changed_at = now; }
    if (raw != stable && now - changed_at >= 40000) {
        stable = raw;
        if (!stable) {
            page = (page + 1) % 3;
            lv_scr_load(pages[page]);
        }
    }
    /* Timestamp-based motion: delays do not permanently slow the animation. */
    if (page == 2) {
        unsigned phase = (unsigned)(now / 10000) % 240;
        int x = 50 + (phase <= 120 ? phase : 240 - phase);
        lv_obj_set_x(ball, x);
    }
}

static void create_ui(void)
{
    const char *titles[] = {"1 Colors", "2 Touch", "3 Render"};
    for (unsigned i = 0; i < 3; ++i) {
        pages[i] = lv_obj_create(NULL);
        lv_obj_remove_style_all(pages[i]);
        lv_obj_set_style_bg_color(pages[i], lv_color_hex(0x181818), 0);
        lv_obj_set_style_bg_opa(pages[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_font(pages[i], &lv_font_montserrat_14, 0);
        lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *title = label(pages[i], titles[i], 0, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 22);
        lv_obj_t *footer = label(pages[i], "BOOT: next", 0, 0);
        lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -22);
    }
    const uint32_t colors[] = {0xff0000, 0x00ff00, 0x0000ff, 0xffffff, 0x808080, 0x000000};
    const char *names[] = {"RED", "GREEN", "BLUE", "WHITE", "GRAY", "BLACK"};
    for (unsigned i = 0; i < 6; ++i) {
        int x = 40 + (i % 2) * 85, y = 52 + (i / 2) * 48;
        lv_obj_t *box = rectangle(pages[0], x, y, 75, 40, colors[i]);
        lv_obj_t *name = label(box, names[i], 0, 0);
        lv_obj_set_style_text_color(name, (i == 1 || i == 3 || i == 4) ? lv_color_black() : lv_color_white(), 0);
        lv_obj_center(name);
    }
    const int positions[4][2] = {{60, 70}, {180, 70}, {60, 170}, {180, 170}};
    for (unsigned i = 0; i < 4; ++i) {
        lv_obj_t *target = rectangle(pages[1], positions[i][0] - 12, positions[i][1] - 12, 24, 24, 0x008080);
        lv_obj_set_style_radius(target, LV_RADIUS_CIRCLE, 0);
        lv_obj_t *name = label(target, i == 0 ? "1" : i == 1 ? "2" : i == 2 ? "3" : "4", 0, 0);
        lv_obj_center(name);
    }
    coordinates = label(pages[1], "Touch the 4 targets", 0, 0);
    lv_obj_align(coordinates, LV_ALIGN_CENTER, 0, 0);
    cursor = rectangle(pages[1], 0, 0, 9, 9, 0xffff00);
    lv_obj_set_style_radius(cursor, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(cursor, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pages[1], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pages[1], touch_event, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(pages[1], touch_event, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(pages[1], touch_event, LV_EVENT_RELEASED, NULL);
    ball = rectangle(pages[2], 50, 100, 20, 20, 0xff8000);
    lv_obj_set_style_radius(ball, LV_RADIUS_CIRCLE, 0);
    stats = label(pages[2], "Waiting for metrics", 0, 0);
    lv_obj_align(stats, LV_ALIGN_CENTER, 0, 35);
    display->driver->monitor_cb = monitor_render;
    lv_scr_load(pages[0]);
    ESP_ERROR_CHECK(lv_timer_create(tick, 20, NULL) ? ESP_OK : ESP_ERR_NO_MEM);
}

void app_main(void)
{
    lv_indev_t *input;
    ESP_ERROR_CHECK(round_display_init(&display, &input));
    const gpio_config_t boot = {
        .pin_bit_mask = 1ULL << GPIO_NUM_9, .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&boot));
    ESP_ERROR_CHECK(lvgl_port_lock(1000) ? ESP_OK : ESP_ERR_TIMEOUT);
    create_ui();
    lvgl_port_unlock();
    ESP_LOGI("diagnostics", "BOOT cycles colors/touch/render. Touch %s", input ? "enabled" : "disabled");
    int64_t previous = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        /* Read/reset monitor counters under the same lock as LVGL callbacks. */
        if (!lvgl_port_lock(100)) continue;
        int64_t now = esp_timer_get_time();
        double seconds = (now - previous) / 1000000.0;
        double hz = refreshes / seconds;
        double average_ms = refreshes ? (double)render_ms / refreshes : 0;
        uint32_t pixels = rendered_pixels;
        if (page == 2) {
            /* LVGL's own printf has float support disabled by default. */
            char text[80];
            snprintf(text, sizeof(text), "LVGL %.1f Hz\n%.2f ms/refresh", hz, average_ms);
            lv_label_set_text(stats, text);
            lv_obj_align(stats, LV_ALIGN_CENTER, 0, 35);
        }
        refreshes = render_ms = rendered_pixels = 0;
        previous = now;
        lvgl_port_unlock();
        ESP_LOGI("diagnostics", "LVGL refresh %.1f/s, avg %.2f ms, pixels=%" PRIu32,
                 hz, average_ms, pixels);
        ESP_LOGI("diagnostics", "heap=%u min=%u largest DMA=%u", (unsigned)esp_get_free_heap_size(),
                 (unsigned)esp_get_minimum_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    }
}

#pragma once
#include "esp_err.h"
#include "lvgl.h"

#define ROUND_DISPLAY_WIDTH 240
#define ROUND_DISPLAY_HEIGHT 240

/* Call once at startup. Owns SPI2 and, when touch is enabled, I2C0.
 * Failures are fatal to the application: use ESP_ERROR_CHECK and reboot.
 * This component does not implement hot removal or retry after partial init.
 * After init, protect application LVGL calls with lvgl_port_lock/unlock.
 */
esp_err_t round_display_init(lv_disp_t **display, lv_indev_t **input);

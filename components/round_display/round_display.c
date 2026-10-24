#include "round_display.h"
#include "sdkconfig.h"
#include "esp_check.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lvgl_port.h"
#include "chsc6x_touch.h"

#if LV_COLOR_DEPTH != 16
#error "Round Display requires LVGL RGB565 (LV_COLOR_DEPTH=16)"
#endif

static const char *TAG = "round_display";

/* Kconfig bools that are off are undefined, so use #if for all of them. */
esp_err_t round_display_init(lv_disp_t **display, lv_indev_t **input)
{
    static bool started;
    ESP_RETURN_ON_FALSE(display && input, ESP_ERR_INVALID_ARG, TAG, "output handles");
    *display = NULL;
    *input = NULL;
    ESP_RETURN_ON_FALSE(!started, ESP_ERR_INVALID_STATE, TAG, "already initialized");
    started = true;

    const spi_bus_config_t bus = {
        .mosi_io_num = 18, .miso_io_num = -1, .sclk_io_num = 19,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = ROUND_DISPLAY_WIDTH * CONFIG_RD_BUFFER_LINES * sizeof(lv_color_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "SPI");
    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = 21, .cs_gpio_num = 1, .pclk_hz = CONFIG_RD_SPI_CLOCK_MHZ * 1000000,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8, .spi_mode = 0, .trans_queue_depth = 10,
    };
    esp_lcd_panel_io_handle_t io;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_cfg, &io), TAG, "LCD IO");
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC, .bits_per_pixel = 16,
#if CONFIG_RD_BGR
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
#else
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
#endif
    };
    esp_lcd_panel_handle_t panel;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_gc9a01(io, &panel_cfg, &panel), TAG, "GC9A01");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "panel init");
    bool invert = false;
#if CONFIG_RD_INVERT_COLORS
    invert = true;
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(panel, invert), TAG, "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "display on");

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL");
    const lvgl_port_display_cfg_t cfg = {
        .io_handle = io, .panel_handle = panel,
        .hres = ROUND_DISPLAY_WIDTH, .vres = ROUND_DISPLAY_HEIGHT,
        .buffer_size = ROUND_DISPLAY_WIDTH * CONFIG_RD_BUFFER_LINES, /* pixels */
        .flags.buff_dma = true,
#if CONFIG_RD_DOUBLE_BUFFER
        .double_buffer = true,
#endif
        .rotation = {
#if CONFIG_RD_LCD_SWAP_XY
            .swap_xy = true,
#endif
#if CONFIG_RD_LCD_MIRROR_X
            .mirror_x = true,
#endif
#if CONFIG_RD_LCD_MIRROR_Y
            .mirror_y = true,
#endif
        },
    };
    *display = lvgl_port_add_disp(&cfg);
    ESP_RETURN_ON_FALSE(*display, ESP_ERR_NO_MEM, TAG, "display buffer");

#if CONFIG_RD_TOUCH_ENABLED
    const i2c_master_bus_config_t i2c_cfg = {
        .i2c_port = I2C_NUM_0, .sda_io_num = 22, .scl_io_num = 23,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t i2c;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_cfg, &i2c), TAG, "I2C");
    ESP_RETURN_ON_ERROR(i2c_master_probe(i2c, 0x2e, 100), TAG, "CHSC6X not responding at 0x2e");
    const chsc6x_touch_config_t touch_cfg = {
        .i2c_bus = i2c, .int_gpio_num = GPIO_NUM_17,
        .x_max = ROUND_DISPLAY_WIDTH, .y_max = ROUND_DISPLAY_HEIGHT,
#if CONFIG_RD_TOUCH_SWAP_XY
        .swap_xy = true,
#endif
#if CONFIG_RD_TOUCH_MIRROR_X
        .mirror_x = true,
#endif
#if CONFIG_RD_TOUCH_MIRROR_Y
        .mirror_y = true,
#endif
    };
    esp_lcd_touch_handle_t touch;
    ESP_RETURN_ON_ERROR(chsc6x_touch_new(&touch_cfg, &touch), TAG, "touch driver");
    const lvgl_port_touch_cfg_t port_touch = {.disp = *display, .handle = touch};
    *input = lvgl_port_add_touch(&port_touch);
    ESP_RETURN_ON_FALSE(*input, ESP_ERR_NO_MEM, TAG, "LVGL input");
#endif
    ESP_LOGI(TAG, "240x240 RGB565, SPI %d MHz, DMA %d bytes/buffer",
             CONFIG_RD_SPI_CLOCK_MHZ, ROUND_DISPLAY_WIDTH * CONFIG_RD_BUFFER_LINES * 2);
    return ESP_OK;
}

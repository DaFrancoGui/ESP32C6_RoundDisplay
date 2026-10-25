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
#include "esp_rom_sys.h"
#include "freertos/task.h"

#if LV_COLOR_DEPTH != 16
#error "Round Display requires LVGL RGB565 (LV_COLOR_DEPTH=16)"
#endif
#if CONFIG_ESP_CONSOLE_UART_DEFAULT
#error "UART0 conflicts with backlight GPIO16 and touch GPIO17; select the USB Serial/JTAG console"
#endif

static const char *TAG = "round_display";

#if CONFIG_RD_TOUCH_ENABLED
/* A USB reset does not power-cycle a slave interrupted during an I2C read. */
static esp_err_t recover_touch_bus(void)
{
    const gpio_num_t sda = GPIO_NUM_22, scl = GPIO_NUM_23;
    const gpio_config_t pins = {
        .pin_bit_mask = (1ULL << sda) | (1ULL << scl),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    /* Set output latches before enabling open drain, avoiding a spurious START. */
    gpio_set_level(sda, 1); /* Open drain: release, never drive either line high. */
    gpio_set_level(scl, 1);
    ESP_RETURN_ON_ERROR(gpio_config(&pins), TAG, "I2C recovery pins");
    vTaskDelay(1); /* Let pull-ups and line capacitance settle before sampling. */
    ESP_LOGI(TAG, "I2C before recovery SDA=%d SCL=%d", gpio_get_level(sda), gpio_get_level(scl));
    for (unsigned i = 0; i < 50 && !gpio_get_level(scl); ++i) {
        vTaskDelay(pdMS_TO_TICKS(2) ? pdMS_TO_TICKS(2) : 1);
    }
    unsigned pulses = 0;
    while (gpio_get_level(scl) && !gpio_get_level(sda) && pulses < 9) {
        gpio_set_level(scl, 0);
        esp_rom_delay_us(5);
        gpio_set_level(scl, 1);
        esp_rom_delay_us(5);
        ++pulses;
    }
    if (pulses && gpio_get_level(scl)) {
        gpio_set_level(scl, 0);
        gpio_set_level(sda, 0);
        esp_rom_delay_us(5);
        gpio_set_level(scl, 1);
        esp_rom_delay_us(5);
        gpio_set_level(sda, 1); /* STOP. */
        vTaskDelay(1);
    }
    int sda_level = gpio_get_level(sda), scl_level = gpio_get_level(scl);
    ESP_LOGI(TAG, "I2C startup SDA=%d SCL=%d recovery clocks=%u", sda_level, scl_level, pulses);
    gpio_reset_pin(sda);
    gpio_reset_pin(scl);
    return sda_level && scl_level ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t init_touch_input(lv_disp_t *display, lv_indev_t **input)
{
    esp_err_t ret = recover_touch_bus();
    if (ret != ESP_OK) return ret;
    i2c_master_bus_handle_t i2c = NULL;
    esp_lcd_touch_handle_t touch = NULL;
    const i2c_master_bus_config_t bus = {
        .i2c_port = I2C_NUM_0, .sda_io_num = 22, .scl_io_num = 23,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_GOTO_ON_ERROR(i2c_new_master_bus(&bus, &i2c), err, TAG, "I2C");
    /* CHSC6X is read directly when INT is asserted. Do not gate startup on a
     * write-address probe: that is not the protocol used by its touch driver. */
    const chsc6x_touch_config_t cfg = {
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
    ESP_GOTO_ON_ERROR(chsc6x_touch_new(&cfg, &touch), err, TAG, "CHSC6X driver");
    const lvgl_port_touch_cfg_t port_touch = {.disp = display, .handle = touch};
    *input = lvgl_port_add_touch(&port_touch);
    ESP_GOTO_ON_FALSE(*input, ESP_ERR_NO_MEM, err, TAG, "LVGL input");
    ESP_LOGI(TAG, "CHSC6X input registered; reads use INT GPIO17 and address 0x2e");
    return ESP_OK;
err:
    if (touch) esp_lcd_touch_del(touch);
    if (i2c) i2c_del_master_bus(i2c);
    return ret;
}
#endif

/* Kconfig bools that are off are undefined, so use #if for all of them. */
esp_err_t round_display_init(lv_disp_t **display, lv_indev_t **input)
{
    static bool started;
    ESP_RETURN_ON_FALSE(display && input, ESP_ERR_INVALID_ARG, TAG, "output handles");
    *display = NULL;
    *input = NULL;
    ESP_RETURN_ON_FALSE(!started, ESP_ERR_INVALID_STATE, TAG, "already initialized");
    started = true;

    /* XIAO D6 is GPIO16, shared with UART0 TX. Use the USB console defaults. */
    const gpio_config_t backlight = {.pin_bit_mask = 1ULL << GPIO_NUM_16, .mode = GPIO_MODE_OUTPUT};
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), TAG, "backlight GPIO");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_16, 1), TAG, "backlight on");

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
    esp_err_t touch_error = init_touch_input(*display, input);
    if (touch_error != ESP_OK) {
        ESP_LOGW(TAG, "Touch startup failed (%s); LCD remains usable with BOOT navigation", esp_err_to_name(touch_error));
    }
#endif
    ESP_LOGI(TAG, "240x240 RGB565, SPI %d MHz, DMA %d bytes/buffer",
             CONFIG_RD_SPI_CLOCK_MHZ, ROUND_DISPLAY_WIDTH * CONFIG_RD_BUFFER_LINES * 2);
    return ESP_OK;
}

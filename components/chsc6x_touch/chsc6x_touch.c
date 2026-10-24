#include "chsc6x_touch.h"
#include "chsc6x_coordinates.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <stdlib.h>

#define CHSC6X_ADDR 0x2e
#define TOUCH_GRACE_US 100000
/* Base must remain first: esp_lcd_touch owns this handle. */
typedef struct {
    esp_lcd_touch_t base;
    i2c_master_dev_handle_t dev;
    i2c_master_bus_handle_t bus;
    SemaphoreHandle_t mutex;
    bool swap_xy, mirror_x, mirror_y;
    int64_t last_touch_us;
} chsc6x_t;

static esp_err_t read_data(esp_lcd_touch_handle_t tp)
{
    chsc6x_t *ctx=(chsc6x_t *)tp;
    if (tp->config.int_gpio_num != GPIO_NUM_NC && gpio_get_level(tp->config.int_gpio_num)) {
        portENTER_CRITICAL(&tp->data.lock);
        tp->data.points=0;
        ctx->last_touch_us=0;
        portEXIT_CRITICAL(&tp->data.lock);
        return ESP_OK;
    }
    uint8_t data[5];
    TickType_t wait=pdMS_TO_TICKS(10);
    if (wait == 0) wait=1;
    esp_err_t ret=ESP_ERR_TIMEOUT;
    if (!ctx->mutex || xSemaphoreTake(ctx->mutex,wait) == pdTRUE) {
        ret=i2c_master_receive(ctx->dev,data,sizeof(data),20);
        if (ret != ESP_OK) i2c_master_bus_reset(ctx->bus);
        if (ctx->mutex) xSemaphoreGive(ctx->mutex);
    }
    int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&tp->data.lock);
    if (ret != ESP_OK) {
        /* Preserve a brief press through contention, then release it. */
        if (!ctx->last_touch_us || now-ctx->last_touch_us > TOUCH_GRACE_US) tp->data.points=0;
    } else if (data[0] == 1) {
        uint16_t x=data[2], y=data[4];
        if (!chsc6x_transform(&x,&y,tp->config.x_max,tp->config.y_max,
                               ctx->swap_xy,ctx->mirror_x,ctx->mirror_y)) {
            tp->data.points=0; ret=ESP_ERR_INVALID_RESPONSE;
        } else {
            tp->data.points=1;
            tp->data.coords[0].x=x;
            tp->data.coords[0].y=y;
            ctx->last_touch_us=now;
        }
    } else { tp->data.points=0; ctx->last_touch_us=0; }
    portEXIT_CRITICAL(&tp->data.lock);
    return ret;
}
static bool get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                   uint16_t *strength, uint8_t *points, uint8_t max_points)
{
    if (!x || !y || !points || !max_points) return false;
    portENTER_CRITICAL(&tp->data.lock);
    *points=tp->data.points ? 1 : 0;
    if (*points) { x[0]=tp->data.coords[0].x; y[0]=tp->data.coords[0].y; if (strength) strength[0]=1; }
    portEXIT_CRITICAL(&tp->data.lock);
    return *points != 0;
}
static esp_err_t destroy(esp_lcd_touch_handle_t tp)
{
    chsc6x_t *ctx=(chsc6x_t *)tp;
    esp_err_t ret=i2c_master_bus_rm_device(ctx->dev);
    if (ret != ESP_OK) return ret;
    if (tp->config.int_gpio_num != GPIO_NUM_NC) gpio_reset_pin(tp->config.int_gpio_num);
    free(ctx);
    return ESP_OK;
}
esp_err_t chsc6x_touch_new(const chsc6x_touch_config_t *cfg, esp_lcd_touch_handle_t *out)
{
    if (!cfg || !out || !cfg->i2c_bus || !cfg->x_max || !cfg->y_max) return ESP_ERR_INVALID_ARG;
    *out=NULL;
    chsc6x_t *ctx=calloc(1,sizeof(*ctx));
    if (!ctx) return ESP_ERR_NO_MEM;
    ctx->bus=cfg->i2c_bus; ctx->mutex=cfg->i2c_mutex;
    ctx->swap_xy=cfg->swap_xy; ctx->mirror_x=cfg->mirror_x; ctx->mirror_y=cfg->mirror_y;
    i2c_device_config_t dev_cfg={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=CHSC6X_ADDR,.scl_speed_hz=100000};
    esp_err_t ret=i2c_master_bus_add_device(ctx->bus,&dev_cfg,&ctx->dev);
    if (ret != ESP_OK) { free(ctx); return ret; }
    if (cfg->int_gpio_num != GPIO_NUM_NC) {
        gpio_config_t io={.mode=GPIO_MODE_INPUT,.pin_bit_mask=1ULL << cfg->int_gpio_num,.pull_up_en=GPIO_PULLUP_ENABLE};
        ret=gpio_config(&io);
        if (ret != ESP_OK) { i2c_master_bus_rm_device(ctx->dev); free(ctx); return ret; }
    }
    ctx->base.data.lock=(portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    ctx->base.config.x_max=cfg->x_max; ctx->base.config.y_max=cfg->y_max;
    ctx->base.config.int_gpio_num=cfg->int_gpio_num; ctx->base.config.rst_gpio_num=GPIO_NUM_NC;
    /* Coordinate transforms happen exactly once, in this driver. Base flags stay zero. */
    ctx->base.read_data=read_data; ctx->base.get_xy=get_xy; ctx->base.del=destroy;
    *out=&ctx->base;
    return ESP_OK;
}

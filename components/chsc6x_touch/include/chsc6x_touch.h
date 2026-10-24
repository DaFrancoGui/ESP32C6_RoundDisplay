/**
 * @file chsc6x_touch.h
 * @brief Driver touch CHSC6X compatível com esp_lcd_touch
 */

#ifndef CHSC6X_TOUCH_H
#define CHSC6X_TOUCH_H

#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configuração do driver CHSC6X
 */
typedef struct {
    i2c_master_bus_handle_t i2c_bus;    ///< Handle do barramento I2C
    gpio_num_t int_gpio_num;             ///< GPIO de interrupção (ou GPIO_NUM_NC)
    uint16_t x_max;                      ///< Resolução X máxima
    uint16_t y_max;                      ///< Resolução Y máxima
    bool swap_xy;                        ///< Primeiro troca X/Y; depois espelha no referencial da tela
    bool mirror_x;                       ///< Espelhar X
    bool mirror_y;                       ///< Espelhar Y
    SemaphoreHandle_t i2c_mutex;         ///< Mutex para proteger barramento I2C compartilhado (opcional)
} chsc6x_touch_config_t;

/**
 * @brief Cria driver touch CHSC6X
 *
 * @param config Configuração do driver
 * @param out_handle Handle de saída
 * @return ESP_OK em sucesso
 */
esp_err_t chsc6x_touch_new(const chsc6x_touch_config_t *config, esp_lcd_touch_handle_t *out_handle);

#ifdef __cplusplus
}
#endif

#endif // CHSC6X_TOUCH_H

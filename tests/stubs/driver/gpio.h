#ifndef TEST_GPIO_H
#define TEST_GPIO_H
#include <stdint.h>
#include "esp_err.h"
enum {
    GPIO_MODE_OUTPUT = 2,
    GPIO_PULLUP_DISABLE = 0,
    GPIO_PULLDOWN_DISABLE = 0,
    GPIO_INTR_DISABLE = 0,
};
typedef struct {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    int intr_type;
} gpio_config_t;
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(int pin, uint32_t level);
#endif

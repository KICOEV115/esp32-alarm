#ifndef OLED_CONFIG_H
#define OLED_CONFIG_H
/* SSD1306 profile already rendered on this user's OLED. */
#define OLED_USE_SH1106 0
#define OLED_I2C_ADDRESS 0x3c
#define OLED_SDA_GPIO 21
#define OLED_SCL_GPIO 22
#if OLED_USE_SH1106
#define OLED_COLUMN_OFFSET 2
#else
#define OLED_COLUMN_OFFSET 0
#endif
#endif

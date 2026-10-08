#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* Classic ESP32 DevKitC / WROOM board. */
#define PIR_GPIO 27
#define ARM_BUTTON_GPIO 32
#define DISARM_BUTTON_GPIO 33

/* GPIO25 -> 1 kohm resistor -> red LED anode; cathode -> GND. */
#define ALARM_LED_GPIO 25

#define PIR_WARMUP_MS 60000
#define INPUT_STABLE_MS 20
#define BUTTON_STABLE_MS 25
#define CONTROL_PERIOD_MS 5

/* GPIO27-to-GND button for timing tests: disconnect PIR OUT first.
 * This mode uses a pull-up and skips sensor warm-up. */
#define DETECTOR_BUTTON_TEST 0

/* Panel-only fault injection. Restore both to zero after testing. */
#define TEST_DROP_FIRST_ACK 0
#define TEST_DROP_EVERY_N_STATE 0

#endif

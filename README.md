# ESP32 Wireless Motion Alarm

A two-button motion alarm written in C with ESP-IDF and FreeRTOS. A detector esp32 reports PIR activity over ESP-NOW. A separate esp32 handles arming, shows status on an OLED, and lights a red LED when an alarm is triggered.

The LED stays on after the sensor returns to CLEAR. Pressing the DISARM button clears the alarm and turns the LED off. Connection faults are shown on the OLED; they do not light the alarm LED.

## Hardware

- Two classic ESP32 DevKitC boards (4 MB flash).
- One PIR module.
- One 128 x 64 I2C OLED using the working SSD1306 profile.
- Two  pushbuttons, one red LED, one 1 kohm resistor, jumper wires, and a breadboard.
- USB power for both boards.

## Build

Used ESP-IDF **6.1**, an ESP-IDF terminal, and a USB serial driver appropriate for the board.

## Design

The detector always reports sensor state, including while the panel is disarmed. The panel owns the decision to latch an alarm.

- A portable state machine implements DISARMED, ARMING, ARMED, and ALARM.
- Status snapshots including a cumulative motion counter.
- Application acknowledgements, bounded retries, sequence numbers, and boot session IDs handle common packet-loss and restart cases.
- A 5 ms control loop handles inputs and decisions. OLED transfers run in a separate, lower-priority task.
- Only changed OLED pages are written. A slow or disconnected screen does not intentionally block the control task.
- LED writes are attempted before display publication. Failed LED writes are retried on subsequent control passes; repeated error logs are suppressed until recovery.

See [architecture](docs/ARCHITECTURE.md), [protocol](PROTOCOL.md), and [testing](docs/TESTING.md) for details.

## Source map

| Directory | Responsibility |
| --- | --- |
| `panel/main`, `detector/main` | Small entry points selecting a role |
| `components/alarm_core` | Packet format, filtering, state machines, retry rules |
| `components/alarm_app` | ESP-IDF hardware integration, radio, LED and OLED |
| `tests` | Host-side C tests, including a mocked GPIO driver |
| `docs` | Architecture, testing and publishing instructions |
| `evidence` | Actual host-test record and physical acceptance checklist |

Pin assignments and input timing are in `components/alarm_app/board_config.h`. OLED settings are in `oled_config.h`. Alarm/link timing constants are in `components/alarm_core/include/alarm_logic.h`.

## Limitations

This is an educational prototype, not a certified security system. Peer discovery is automatic and traffic is not authenticated/encrypted.

There is no measured end-to-end latency or range guarantee. The panel and detector have independent clocks, so their log timestamps cannot simply be subtracted to measure one-way wireless delay. Motion counters are volatile.

## Publishing

## References

- [ESP-IDF 6.1 build system](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-guides/build-system.html)
- [ESP-NOW API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/network/esp_now.html)
- [GPIO API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/peripherals/gpio.html)
- [I2C API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/peripherals/i2c.html)

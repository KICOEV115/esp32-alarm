# ESP32 Wireless Motion Alarm

A two-node motion alarm written in C with ESP-IDF and FreeRTOS. A detector reports PIR activity over ESP-NOW. A separate panel handles arming, shows status on an OLED, and lights a red LED when an alarm is latched.

The LED stays on after the sensor returns to CLEAR. Pressing DISARM clears the alarm and turns the LED off. Connection faults are shown on the OLED; they do not flash the alarm LED.

## Hardware

- Two classic ESP32 DevKitC / ESP32-WROOM boards (4 MB flash).
- One PIR module with a 3.3 V-compatible digital output.
- One 128 x 64 I2C OLED using the working SSD1306 profile, address 0x3C.
- Two normally-open pushbuttons, one red LED, one 1 kohm resistor, jumper wires, and a breadboard.
- USB power for both boards.

This pin map targets the classic ESP32, not ESP32-C3/S3 boards. Check the board's printed GPIO labels rather than counting physical pins.

## Wiring

Disconnect power while changing wiring. Each device shares ground with its own ESP32. The two wireless nodes do not need a ground wire between them.

### Detector

| Part terminal | Detector connection |
| --- | --- |
| PIR OUT | GPIO27 |
| PIR GND | GND |
| PIR VCC | 5V for the module used in this prototype; check your module's specification |

The GPIO input must remain 3.3 V-compatible even if the PIR module is powered from 5 V. The firmware treats HIGH as motion.

### Panel

| Part terminal | Panel connection |
| --- | --- |
| OLED VCC | 3V3 |
| OLED GND | GND |
| OLED SDA | GPIO21 |
| OLED SCL | GPIO22 |
| ARM button | Between GPIO32 and GND |
| DISARM button | Between GPIO33 and GND |
| Red LED anode (long leg) | GPIO25 through a 1 kohm series resistor |
| Red LED cathode (short leg / flat-edge side) | GND |

The LED has no connection to the 3V3 rail. GPIO25 supplies its controlled current through the resistor. Place the LED legs in separate breadboard connection groups. For four-leg buttons, select terminals on opposite switch contacts; do not assume every adjacent pair is switched.

## Build and flash

Use ESP-IDF **6.1**, an activated ESP-IDF terminal, and a USB serial driver appropriate for your board. CMake 3.22 or newer is required by this IDF release. No Arduino framework is used.

Extract the complete project to a new folder. Do not overlay it on an old build directory. Keep `components`, `panel`, and `detector` together.

### Panel (Windows example)

From the directory containing this README:

```sh
cd panel
idf.py set-target esp32
idf.py build
idf.py -p COM5 flash monitor
```

Replace COM5 with the panel's actual port. Exit the monitor with Ctrl+] before flashing another board.

### Detector

From the panel directory, connect/select the detector board and run:

```sh
cd ../detector
idf.py set-target esp32
idf.py build
idf.py -p COM6 flash monitor
```

COM6 is only an example. If you connect boards one at a time, Windows may reuse the same port. Verify `role=PANEL` or `role=DETECTOR` in startup logs. Run `set-target` for a fresh project; subsequent edits normally need only build and flash.

There is deliberately no root `CMakeLists.txt`: build inside **panel** or **detector**. On Linux, use the appropriate serial device instead of a COM port.

## First run

1. Power both boards. The panel starts DISARMED with the LED off.
2. Wait for `LINK: ONLINE` and the detector's 60-second startup warm-up.
3. Press ARM. The 10-second exit countdown begins; the LED remains off.
4. After ARMED appears, trigger the PIR. The OLED shows ALARM and the LED turns on.
5. Wait for the PIR to return LOW. ALARM and the LED remain on.
6. Press DISARM. The panel returns to DISARMED and the LED turns off.

Expected output messages include `Alarm LED ready: GPIO25, initially OFF`, `ALARM LED ON`, and `ALARM LED OFF`. There is no startup LED pulse or diagnostic delay.

If the screen says ARM DISABLED, read the line above it: WAIT FOR DETECTOR, PIR READY IN, or WAIT FOR LINK ACK identifies the missing condition. A detector restart fault requires DISARM before rearming. An existing alarm survives link loss; a panel power cycle starts disarmed.

## Design

The detector always reports sensor state, including while the panel is disarmed. The panel owns the decision to latch an alarm.

- A portable state machine implements DISARMED, ARMING, ARMED, and ALARM.
- Status snapshots include a cumulative motion counter. A later snapshot can reveal a missed HIGH event even after the input has returned LOW.
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

## Verification status

Five host C test programs cover protocol validation, system behaviour, rendering, OLED transfers, and the LED output path. Run from the repository root:

```sh
python tests/run_tests.py
```

Python 3 and a host C compiler (`cc`, `gcc`, or `clang`) are required. The ESP32 cross-compiler cannot substitute for a compiler that creates programs for your computer. See [recorded results](evidence/HOST_TEST_RESULTS.txt).

This release has not been target-built or bench-tested in the preparation environment, which has no ESP-IDF toolchain or boards. Earlier firmware was exercised on physical boards, and GPIO25 was reported to light an LED, but that is not a completed acceptance test of this release. The [bench checklist](evidence/BENCH_TESTS.md) records what remains to verify.

GitHub Actions is configured to run host tests and build both roles with the ESP-IDF 6.1 container. A workflow file is not a passing CI result: inspect its first run after publishing.

## Limitations

This is an educational prototype, not a certified security system. Peer discovery is automatic and traffic is not authenticated/encrypted by this application. CRC and sequence checks do not prevent an attacker from forging messages. The receiver remembers only a bounded set of retired sessions.

PIR warm-up, hold time and retrigger behaviour affect the physical response. There is no measured end-to-end latency or range guarantee. The panel and detector have independent clocks, so their log timestamps cannot simply be subtracted to measure one-way wireless delay. Motion counters are volatile; a restart loses detector history. A delayed counter update near an arming boundary may conservatively trigger an alarm.

## Publishing

See [GitHub setup](docs/GITHUB.md). Add your own build/bench results and demonstration media as they become available. No photographs, demo links, CI badges, author identity, or measured results have been invented. A redistribution license has not been selected; choose one before advertising the repository as open source.

## References

- [ESP-IDF 6.1 build system](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-guides/build-system.html)
- [ESP-NOW API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/network/esp_now.html)
- [GPIO API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/peripherals/gpio.html)
- [I2C API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/peripherals/i2c.html)

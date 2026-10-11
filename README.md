# ESP32 Wireless Motion Alarm

A college learning project using two ESP32 boards. One reads a PIR motion sensor; the other shows the system status on an OLED and turns on an alarm LED. The firmware is written in C with ESP-IDF, FreeRTOS and ESP-NOW.

## Why?

I used this project to practise connecting hardware, reading inputs, sending wireless messages and debugging with serial logs. Keeping the detector and panel separate lets them communicate without a cable or Wi-Fi router.

## How it works

1. Both boards start; the PIR warms up for 60 seconds.
2. Press ARM on the panel to start a 10-second countdown.
3. Once armed, motion triggers ALARM and turns on the LED.
4. The alarm stays active after the sensor clears.
5. Press DISARM to clear the alarm and turn off the LED.

The OLED also reports connection faults. Messages include sequence numbers, acknowledgements and a motion counter to help handle missed or repeated packets.

## Architecture

|Folder|Purpose|
|-|-|
|`detector/`|Sensor-node entry point|
|`panel/`|Control-panel entry point|
|`components/alarm\_core/`|Alarm rules and message protocol|
|`components/alarm\_app/`|GPIO, radio and OLED integration|
|`tests/`|Tests that run on a computer|

The control loop handles inputs and the LED. A separate task updates the OLED.

## Wiring

|Board|Connections|
|-|-|
|Detector|PIR OUT → GPIO27; GND → GND; supply according to the sensor specification|
|Panel|OLED SDA → GPIO21; SCL → GPIO22; VCC → 3V3; GND → GND|
|Panel buttons|ARM between GPIO32 and GND; DISARM between GPIO33 and GND|
|Panel LED|GPIO25 → 1 kohm resistor → LED anode; cathode → GND|

PIR output must be 3.3 V-compatible. Disconnect power before rewiring. Pins are configured in `components/alarm\_app/board\_config.h`.

## Build and flash

Open an ESP-IDF terminal at the repository root:

```sh
cd panel
idf.py set-target esp32
idf.py build
idf.py -p COM5 flash monitor
```

Exit the monitor with Ctrl+]. Repeat inside `detector/` for the other board. Replace COM5 with the correct port. Build inside either role folder; the repository root has no CMakeLists.txt.

## Drawbacks

* PIR warm-up and retrigger behaviour can make detection feel slow.
* Communication is not authenticated or encrypted.
* State is lost on restart; range and end-to-end latency have not been measured.

## Complexity

State updates are `O(1)`. CRC is `O(n)` for `n` input bytes, but packets are fixed at 26 bytes. Session filtering scans at most four retired IDs. Frame rendering is `O(B + C)` with the fixed font, frame comparison is `O(B)`, and changed-page transfers are `O(8 + P × W)`: `B` is framebuffer bytes, `C` is characters, `P` is changed pages and `W` is width.


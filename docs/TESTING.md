# Testing

## Host tests

From the root, run `python tests/run_tests.py`. A native C compiler is required. `CC` can select a compiler executable. Tests are compiled with C11, warnings as errors and optimization; temporary executables are removed afterward.

| Program | Coverage |
| --- | --- |
| Protocol | CRC, malformed fields, length/version checks, sequence wrap, duplicate/old sessions |
| System | Countdown, latching, reboot faults, debounce, warm-up, retries and lost-event recovery |
| UI | Alarm/fault/link combinations, framebuffer bounds, dirty pages, countdown text |
| OLED wire | Command/data packets, page offsets, partial transfer failures |
| Outputs | Real output module against GPIO stubs: startup LOW, ALARM HIGH, CLEAR/link-loss latch, DISARM LOW, initialization failures and write retries |

The stubs live only under `tests/stubs`. They are not registered as ESP-IDF components and are never included in the firmware build. Their tests check application behaviour, not electrical characteristics or complete SDK compatibility.

## Firmware builds

Run `idf.py build` in each role directory using ESP-IDF 6.1. The included GitHub workflow does the same with the official versioned container, plus the native host tests. Neither a successful host run nor a workflow definition replaces a successful target build.

## Bench acceptance

Use [the checklist](../evidence/BENCH_TESTS.md). Record firmware revision, board role, power source, sensor model if known, and actual outcomes. Film at least ten complete arm/trigger/disarm cycles if demonstrating repeatability. Leave results unmeasured until tested.

For packet-loss experiments, set panel-only `TEST_DROP_FIRST_ACK` or `TEST_DROP_EVERY_N_STATE` in `board_config.h`. Restore both to zero and rebuild afterward. Watch `retries`, `duplicates`, `unconfirmed`, queue-overflow and send-error counters; do not count a retry as a new motion event.

For sensor-independent timing, disconnect PIR OUT and set `DETECTOR_BUTTON_TEST` to 1 on the detector. Connect a normally-open button between GPIO27 and GND. This enables an active-low input with no warm-up. Restore the flag to zero before reconnecting the PIR.

## Timing interpretation

`stable-input-to-TX` begins after input filtering and ends at submission to the radio API. `RX-to-output-call` covers local callback arrival through the output call; it does not measure physical LED light or prove a GPIO write succeeded. `RX-to-write-done` ends after the display transaction completes, not necessarily the instant the pixels become visible. Values from different boards use unsynchronized clocks. Measure the complete physical path with video or instrumentation; do not add unrelated timings or quote one sample as a guaranteed latency.

## Troubleshooting

- **CMakeLists not found:** enter `panel` or `detector`, not the repository root.
- **ARM disabled:** check the OLED detail line and power both nodes; allow sensor warm-up.
- **Wrong role:** check the startup role and serial port before flashing again.
- **LED stays dark:** verify a red LED's polarity, GPIO25 label, common panel GND, series resistor and separate breadboard contact groups. A log is not an electrical measurement.
- **LED always on:** ensure its anode supply is GPIO25 through the resistor, not 3V3. Confirm the panel is DISARMED.
- **Buttons unreliable:** verify the two switch contacts and ground. Four-leg switches have internally joined pin pairs.
- **Blank OLED:** confirm 3V3/GND, SDA21/SCL22 and address/profile. The default profile is the one used on this prototype, not universal controller detection.

There is no startup output pulse in this release. Restore the source version under test after any temporary diagnostics.

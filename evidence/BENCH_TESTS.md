# LED release bench checklist

Status: pending physical acceptance of this release. Host results are recorded separately. Earlier development observations are not substitutes for rerunning these checks on this source version.

| Test | Expected result | Recorded result |
| --- | --- | --- |
| Build detector and panel with ESP-IDF 6.1 | Both compile/link | Pending |
| Power panel | DISARMED, LED off; no startup pulse | Pending |
| Power detector | Link online; warm-up shown; ARM blocked until ready | Pending |
| ARM then DISARM during countdown | Countdown cancels; LED remains off | Pending |
| Arm, wait, trigger motion | ALARM displayed; LED on | Pending |
| Sensor returns CLEAR | ALARM and LED stay on | Pending |
| DISARM after alarm | DISARMED; LED off | Pending |
| Disconnect detector while disarmed | OLED link fault; LED stays off | Pending |
| Disconnect detector during ALARM | LED remains on; OLED shows offline link | Pending |
| Restart detector while armed | Restart fault; DISARM required | Pending |
| Drop first ACK using test flag | Retry/duplicate counted; no repeated alarm event | Pending |
| Lose motion snapshot | Later cumulative count can recover event while armed | Pending |
| Disconnect OLED with power off, then restart | Control logs and LED still operate after link/warm-up | Pending |
| Ten normal alarm cycles | Record each success/failure, not a claimed percentage | Pending |

For each run save date, source revision, configuration, wiring photo, serial logs and relevant video. Record response time and range only when actually measured. Remove private background information from published photos/logs.

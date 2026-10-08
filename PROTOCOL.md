# Application protocol v3

Exactly 26 bytes, big-endian integers. This application version is separate
from the ESP-NOW transport's own v1/v2 version numbers.

| Byte offsets | Field |
| --- | --- |
| 0–1 | ASCII WA |
| 2 | Application version 3 |
| 3 | Type: STATE=1, ACK=2 |
| 4 | Sender role: DETECTOR=1, PANEL=2 |
| 5 | Detector flags: READY bit 0, HIGH bit 1 |
| 6–9 | Session: random boot ID; ACK echoes the acknowledged sender's session |
| 10–13 | Sequence: increasing per STATE; ACK echoes the acknowledged sequence |
| 14–17 | Detector cumulative rising-edge count |
| 18–21 | Motion age at snapshot creation, in detector milliseconds; UINT32_MAX = no event |
| 22–23 | Sensor warm-up seconds remaining |
| 24–25 | CRC-16/CCITT-FALSE over bytes 0–23 |

ACK contains only role, echoed session and sequence; flags/count/age/warm-up
are zero. Panel STATE has flags/count/warm-up zero and age UINT32_MAX.
Warming detector STATE has no motion flags/count/event age. READY requires
zero warm-up remaining. Unknown fields/roles/types, reserved flags and invalid
combinations are rejected.

The receiver validates length, magic, application version, CRC and fields
before touching alarm state. It filters source MAC and opposite role.
A STATE is applied once in a session; same/older sequence numbers are suppressed
and acknowledged again. Sequence/count comparisons use modulo distance within
half the 32-bit space. Four retired boot sessions reject delayed old-session
packets; this bounded cache is not a security mechanism.

STATE changes preempt pending heartbeats. The detector's cumulative count
survives this coalescing and all later snapshots within the same boot.
An ACK names the latest pending STATE and establishes application reception.
Send-completion callbacks alone do not establish it. Link health requires
recent peer STATE and a matching application ACK in both directions.

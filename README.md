# nRF24L01 Hayes AT Modem

A full Hayes-compatible AT command modem emulator for Arduino, using the nRF24L01+ radio module as the wireless link. Drop-in compatible with any serial terminal that speaks AT commands — connect two Arduinos and get a wireless serial pipe with proper modem semantics: dialling, answering, flow control, keep-alive, and status LEDs.

Designed and tested on the **RF-Nano** (Arduino Nano with onboard nRF24L01+), and compatible with any Arduino Uno / Nano paired with an external nRF24L01+ module.

Current firmware version: **v1.34.0**

---

## Features

- **Full Hayes AT command set** — `ATD`, `ATA`, `ATO`, `ATH`, `ATI`, `ATE`, `AT&F`, `AT&Z`, `AT&Y`
- **Three link modes** — Transparent pipe (no framing), Hardware ACK, or Software Flow Control (**default**)
- **Cooperative half-duplex** — automatic TX yield token in SWFLOW mode gives each side a clean transmit window during simultaneous bidirectional transfers
- **S-registers S0–S17** covering all timing, retry, keep-alive, flow control, and scanner parameters — all saved to EEPROM with write-if-different (`EEPROM.update`)
- **XON/XOFF flow control** on both serial and radio links (S13, default ON)
- **Keep-alive / ping-pong** with configurable interval and miss threshold (S10–S12)
- **Channel busy detection** before dialling — configurable scan duration (S14, S15)
- **Spectrum analyser** — `ATSPECTRUM` sweeps all 126 channels and renders ASCII signal density
- **Autodial on startup** — store up to 4 dial strings, set one to fire 2 s after boot
- **8 status LEDs** mimicking a real modem front panel (MR, TR, OH, CD, SD, RD, HS, ER)
- **Radio health monitoring** — detects disconnected nRF24L01 at runtime and on boot
- **Data loss counters** — tracks dropped bytes in both directions, reported by `ATI`
- **Configurable baud rate** 9600–1 000 000, saved to EEPROM (`ATSBAUD`)
- **Factory reset** (`AT&F`) restores all defaults and rewrites EEPROM

---

## Hardware

### RF-Nano (recommended)

The RF-Nano has the nRF24L01+ soldered directly onto the board. No external wiring needed for the radio — just add LEDs.

| nRF24L01+ signal | Arduino pin |
|---|---|
| CE | D7 *(hardwired on RF-Nano)* |
| CSN | D8 *(hardwired on RF-Nano)* |
| MOSI | D11 |
| MISO | D12 |
| SCK | D13 |

### Arduino Uno / Nano + external nRF24L01+

Wire the module as above but use D7 → CE and D8 → CSN (or change `CE_PIN` / `CSN_PIN` in the sketch). Add a 10–100 µF capacitor across the module's VCC/GND to stabilise the 3.3 V supply.

### Status LEDs

Connect each LED via a **330 Ω resistor** to GND (anode to pin, cathode via resistor to GND).

| LED | Pin | Colour | Meaning |
|---|---|---|---|
| **MR** Modem Ready | A0 | Green | Radio initialised and healthy |
| **TR** Terminal Ready | A1 | Green | Serial activity in last 200 ms |
| **OH** Off Hook | A2 | Yellow | Any non-idle state |
| **CD** Carrier Detect | A3 | Yellow | Connected / data mode |
| **SD** Send Data | A4 | Red | Flashes 40 ms per TX packet |
| **RD** Receive Data | A5 | Red | Flashes 40 ms per RX packet |
| **HS** High Speed | D2 | Green | 2 Mbps mode active |
| **ER** Error | D3 | Red | TX failure / NO CARRIER / data loss |

> **MR** goes dark and **ER** blinks continuously if the nRF24L01 fails to initialise or is disconnected at runtime.

---

## Dependencies

Install via **Sketch → Include Library → Manage Libraries**:

- [RF24 by TMRh20](https://github.com/nRF24/RF24) — nRF24L01 driver

---

## Getting Started

1. Flash the same sketch to **both** Arduinos.
2. Open a serial terminal at **115 200 baud** (default).
3. Set each node's MAC address:
   ```
   ATSMYMAC=A1B2C3    → OK
   ATSMYMAC=D4E5F6    → OK   (on the other node)
   ```
4. On node B, optionally enable auto-answer:
   ```
   ATS0=1             → OK
   ```
5. From node A, dial node B:
   ```
   ATD D4E5F6         → CONNECT
   ```
6. Type freely — data flows both ways. Use `+++` (with 1 s silence either side) to return to command mode without hanging up. Type `ATO` to return to data mode.

---

## AT Command Reference

### Connection

| Command | Description |
|---|---|
| `ATD XXYYZZ` | Dial remote MAC `XXYYZZ` (6 hex digits). Returns `BUSY` if channel occupied and S14=1. |
| `ATA` | Manually answer an incoming call |
| `ATO` | Return to data mode after `+++` escape |
| `ATH` | Hang up active connection (`NO CARRIER`) or reject incoming ring (`OK`) |
| `ATS0=n` | Auto-answer after *n* rings (0 = off) |
| `+++` | Escape from data mode to command mode (1 s guard each side) |

### Configuration

| Command | Description |
|---|---|
| `ATI` | Print full modem status — radio health, connection state, MACs, RSSI, all S-registers, drop counters |
| `ATE0` / `ATE1` | Echo off / on |
| `ATSMYMAC=XXYYZZ` | Set own 3-byte MAC, saved to EEPROM |
| `ATSMYMAC?` | Query own MAC |
| `ATSETCH=nn` | Set RF channel 0–125 (default 97 = 2497 MHz, above WiFi and Bluetooth) |
| `ATSETCH?` | Query RF channel |
| `ATSSPEED=n` | Set air data rate: `0`=250 kbps  `1`=1 Mbps (default)  `2`=2 Mbps |
| `ATSSPEED?` | Query air data rate |
| `ATSBAUD=n` | Set serial baud rate. `OK` sent at old rate, then port switches — match your terminal. |
| `ATSBAUD?` | Query current baud rate |
| `ATSFLOW=n` | Set link mode: `0`=transparent  `1`=HWACK  `2`=SWFLOW (**default**) |
| `ATSFLOW?` | Query link mode |
| `ATSPECTRUM` | Sweep all 126 channels and display ASCII spectrum (any key to stop) |
| `AT&F` | Factory reset — restores all defaults, rewrites EEPROM |

### Stored Dial Strings & Autodial

| Command | Description |
|---|---|
| `AT&Zn=string` | Store dial string in slot *n* (0–3). Empty string clears slot. |
| `AT&Zn?` | Query stored string in slot *n* |
| `AT&Yn` | Set startup autodial slot (fires 2 s after boot) |
| `AT&Y?` | Query startup slot (`none` if disabled) |

### S-Registers

Query with `ATSn?` (returns zero-padded 3 digits). Set with `ATSn=value`. All saved to EEPROM.

| Register | Default | Range | Description |
|---|---|---|---|
| S0 | 0 | 0–255 | Auto-answer ring count (0 = disabled) |
| S6 | 0 | 0–255 s | Pre-dial wait (0 = none; non-blocking) |
| S7 | 3 | 1–255 s | Carrier wait per dial attempt |
| S8 | 3 | 0–255 | Dial retry attempts after first failure (0 = no retry) |
| S9 | 3 | 0–255 s | Inter-retry interval |
| S10 | 1 | 0–1 | Keep-alive enable (1 = on) |
| S11 | 5 | 1–255 s | Keep-alive ping interval |
| S12 | 3 | 1–255 | Missed pings before `NO CARRIER` |
| S13 | 1 | 0–1 | Serial/radio flow control: `0`=none  `1`=XON/XOFF |
| S14 | 1 | 0–1 | Busy detect before dial: `0`=off  `1`=on |
| S15 | 50 | 1–255 | Busy detect sample count (each ~2 ms) |
| S16 | 5 | 1–255 ms | Transparent mode TX idle flush timeout |
| S17 | 20 | 1–255 | Spectrum scan samples per channel (each ~500 µs) |

> With default S7=3, S8=3, S9=3: total dial window = 4 attempts × 3 s + 3 gaps × 3 s = **21 seconds** before final `NO CARRIER`.

---

## Supported Baud Rates

| Value | Notes |
|---|---|
| 9600 | Minimum; radio is starved |
| 19200 | |
| 38400 | |
| 57600 | Well matched to HWACK mode (~50 kbps) |
| **115200** | **Default** — best match for SWFLOW mode |
| 250000 | Exceeds radio ceiling; serial no longer bottleneck |
| 500000 | Recommended for maximum throughput |
| 1000000 | Works on CH340G; verify with your USB-serial chip |

---

## Link Modes

### SWFLOW mode (`ATSFLOW=2`, **default**)

Auto-ACK disabled. Packets fly continuously up to a 4-slot retransmit window. The receiver sends `PKT_NACK` on gaps and `PKT_SWACK` on each in-order packet. A 2 s safety-net retransmit timer catches packets lost when both data and NACK drop. Throughput ~80–100 kbps on a clean link.

SWFLOW mode also enables **cooperative half-duplex** (see below).

### HWACK mode (`ATSFLOW=1`)

Hardware acknowledgement via the nRF24L01's built-in auto-ACK and retry mechanism. Every 32-byte packet is confirmed before the next is sent. Reliable on noisy links; throughput limited to ~50 kbps by the ACK turnaround. Cooperative duplex is **not available** in HWACK mode.

### Transparent mode (`ATSFLOW=0`)

No framing, no ACK, no protocol. `ATD` connects instantly without handshake. Every byte received on serial is transmitted over radio; every byte received from radio is written to serial. The application is responsible for all framing, checksums, and flow control. Ideal for bridging existing protocols.

`AT&Dn` dial strings and `+++`/`ATH` still work. Packets are always 32 bytes — the receiver sees all 32 bytes regardless of how many were sent (zero-padded). Use S16 to tune the TX flush timeout.

**Maximum throughput settings:**
```
ATSSPEED=2        (2 Mbps air rate)
ATSFLOW=2         (software flow control — default)
ATSBAUD=500000    (serial ceiling well above radio limit)
```

---

## Cooperative Half-Duplex (SWFLOW only)

The nRF24L01 is a half-duplex radio — it cannot transmit and receive simultaneously. Without coordination, both sides transmitting at the same time causes collisions and silent data loss.

In SWFLOW mode the modem uses a **TX yield token** piggybacked onto every software ACK:

- When side B receives a packet from A and has **nothing to send** → replies with plain `PKT_SWACK`. A continues transmitting.
- When side B receives a packet and **has data queued** → replies with `PKT_SWACK_YIELD`. A waits for B's packet to arrive before sending again.
- B sends its packet, ACKs back — if A still has data it yields back, and the token alternates packet by packet.

```
Both sides have data:

A ──[data seq=5]────────────────────────────────► B
A ◄──────────────────────[SWACK_YIELD seq=5]──── B  (B has data)
A  waits for B's packet...
B ──[data seq=0]────────────────────────────────► A
B ◄──────────────────────[SWACK_YIELD seq=0]──── A  (A still has data)
A ──[data seq=6]────────────────────────────────► B
... alternates until one side's buffer drains
```

No configuration required — always active in SWFLOW mode. Under one-way traffic there is zero overhead.

---

## Dial Sequence

```
ATD XXYYZZ
  → S14=1: scan channel for busy (S15 samples × ~2 ms)
  → attempt 1: send PKT_CONN, wait S7 s for PKT_ACK
      PKT_ACK received → CONNECT
      S7 timeout → NO CARRIER - retry 1/S8, wait S9 s
  → attempt 2 ... attempt S8+1
  → NO CARRIER  (final)
```

On the receiver, `RING` prints once per received `PKT_CONN` packet. The receiver stops ringing automatically when the caller's retry window expires.

---

## Keep-Alive

When `S10=1`, the **dialling side** (initiator) sends `PKT_PING` every S11 seconds. The **answering side** always replies with `PKT_PONG` regardless of its own S10 setting.

| Side | `S10=1` | `S10=0` |
|---|---|---|
| **Initiator** | Sends pings, counts missed pongs, drops after S12 misses | No pings sent |
| **Answerer** | Replies to all pings AND tracks silence — drops after `S11 × S12` s with no ping | Replies to all pings, never drops due to silence |

The answerer's timeout (`S11 × S12` seconds, default 15 s) matches the initiator's total retry window so both sides disconnect at approximately the same time.

---

## Flow Control

XON/XOFF is active by default (`S13=1`). The modem sends `0x13` (XOFF) when its RX buffer exceeds 75% (192 bytes) and `0x11` (XON) when it drains below 25% (64 bytes). The same thresholds apply over the radio link.

Set `ATS13=0` if your terminal does not honour XON/XOFF — `0x11`/`0x13` will then pass through as plain data. Buffer overflow is tracked and reported by `ATI`:

```
TX drop : 142 bytes (serial->radio, host overflow)
RX drop : 0 bytes (radio->serial, radio overflow)
** DATA LOSS DETECTED — consider enabling XON/XOFF (ATS13=1) **
```

The **ER LED** flashes on every dropped byte. Counters reset after each `ATI` display.

---

## Spectrum Analyser

```
ATSPECTRUM
```

Sweeps all 126 nRF24L01 channels (2400–2525 MHz), performs S17 independent RPD measurements per channel with reliable latch-reset cycling, and streams an ASCII density display:

```
Ch:  0         1         2    ...   12
     0123456789012345678901234 ... 3456
     @@@@@@@@@@@@@@@@@@@@#*+:      @@@@@@@@@@@@@@@@@@@@@@@@       @@@@@@@@@@@@@@@@@@@@@
```

Density characters: ` ` (0%) → `.` → `:` → `-` → `=` → `+` → `*` → `#` → `@` → `%` (100%)

A ruler header is printed at the start and repeated every 40 sweeps. Any keypress stops the scan. Tune S17 for faster (lower) or more reliable (higher) scans — default 20 samples per channel gives ~1.3 s per sweep.

WiFi channels 1, 6, 11 appear as dense blocks around nRF24 channels 1, 26, 51. The default operating channel 97 (2497 MHz) is above all WiFi and Bluetooth.

---

## Packet Protocol

All packets are fixed 32 bytes (nRF24L01 payload size). Header is 3 bytes, leaving 29 bytes of user payload per packet in SWFLOW/HWACK modes.

```
Byte 0  — packet type
Byte 1  — sequence number (0–255, data-only counter in SWFLOW)
Byte 2  — payload length (0–29)
Bytes 3–31 — payload
```

| Type | Value | Description |
|---|---|---|
| PKT_DATA | 0x01 | User data |
| PKT_XON | 0x02 | Resume sending (flow control) |
| PKT_XOFF | 0x03 | Pause sending (flow control) |
| PKT_DISC | 0x04 | Disconnect / hang up |
| PKT_CONN | 0x05 | Connection request (carries caller MAC) |
| PKT_ACK | 0x06 | Connection accepted |
| PKT_NACK | 0x07 | SWFLOW: retransmit request for missing seq |
| PKT_SWACK | 0x08 | SWFLOW: cumulative ACK |
| PKT_PING | 0x09 | Keep-alive ping (initiator → answerer) |
| PKT_PONG | 0x0A | Keep-alive reply (answerer → initiator) |
| PKT_SWACK_YIELD | 0x0B | SWFLOW: cumulative ACK + TX yield request |

Pipe addresses are derived from the 3-byte MAC: `0xAB 0xCD [mac0] [mac1] [mac2]` — all 3 MAC bytes used directly for collision-free addressing.

---

## EEPROM Layout

93 bytes used (addresses 0–92), magic byte `0xA5` at offset 8. All writes use `EEPROM.update()` — bytes are only physically written when the value changes, protecting against wear.

| Offset | Size | Contents |
|---|---|---|
| 0–2 | 3 B | Own MAC |
| 3–5 | 3 B | Remote MAC (last dialled) |
| 6 | 1 B | RF channel |
| 7 | 1 B | Speed enum (0/1/2) |
| 8 | 1 B | Magic byte (`0xA5`) |
| 9 | 1 B | S0 |
| 10 | 1 B | Flow mode (ATSFLOW) |
| 11 | 1 B | Baud index |
| 12–79 | 68 B | Dial strings × 4 (17 bytes each) |
| 80 | 1 B | Startup autodial slot |
| 81–92 | 12 B | S6–S17 |

If the magic byte is invalid on boot, all settings stay at compile-time defaults and EEPROM is left untouched until the first `saveConfig()` call.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| **MR off, ER blinking at boot** | nRF24L01 not detected | Check wiring / power supply |
| **ER flashing during transfer** | Buffer overflow / data loss | Enable XON/XOFF (`ATS13=1`) or reduce baud |
| **BUSY on ATD after disconnect** | RPD latch not cleared | Normal — 200 ms grace period; wait briefly then retry |
| **NO CARRIER immediately on ATD** | Remote not listening, wrong channel, no answer within S7×(S8+1) | Check `ATSETCH` matches; check remote is idle |
| **RING keeps printing after TX gone** | Ring timeout not expired | Stops automatically after `(S8+1)×S7 + S8×S9 + 2` s |
| **Garbled output after ATSBAUD** | Terminal not switched to new rate | Change terminal baud to match, or reboot to reload from EEPROM |
| **Connection drops after idle** | Keep-alive timeout | Set `ATS10=0` to disable, or increase S11/S12 |
| **Binary transfers corrupt** | XON/XOFF intercepting 0x11/0x13 bytes | Set `ATS13=0` |
| **No data RX→TX after reconnect** | Stale SWFLOW sequence numbers | Fixed in v1.29.0+ — ensure both nodes run same firmware version |
| **Poor duplex throughput** | HWACK mode, no collision avoidance | Switch to SWFLOW (`ATSFLOW=2`, default) |
| **ATSPECTRUM shows no activity** | RPD latch issue | Ensure radio is in IDLE state; try increasing S17 |

---

## Version History

| Version | Changes |
|---|---|
| v1.0 | Initial release |
| v1.1–v1.10 | SWFLOW mode, keep-alive, flow control, busy detect, spectrum scan, transparent mode |
| v1.11–v1.20 | S-register expansion (S14–S17), ATSFLOW rename, RPD fixes, BUSY-after-ATH fix |
| v1.21–v1.25 | Ring timeout, txPop wrap bug fix (root cause of data corruption), RING on PKT_CONN |
| v1.26–v1.30 | ATH result code fix, ATO command, SWFLOW session reset on reconnect, code review cleanup |
| v1.31 | EEPROM.update, parseMac cleanup, non-blocking S6, macToAddr uses all 3 MAC bytes |
| v1.32 | Spectrum footer ruler every 40 sweeps |
| v1.33 | Spectrum scan rewrite — per-sample stop/start/settle, percentage density mapping |
| v1.34 | Cooperative yield fix — active wait for remote packet instead of single-loop skip |

---

## License

MIT — free to use, modify, and distribute. Attribution appreciated.

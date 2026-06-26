# nRF24L01 Hayes AT Modem

A full Hayes-compatible AT command modem emulator for Arduino, using the nRF24L01+ radio module as the wireless link. Connect two Arduinos and get a wireless serial pipe with proper modem semantics: dialling, answering, flow control, keep-alive, diagnostics, and status LEDs — or configure both as a completely silent, invisible wireless bridge.

Designed and tested on the **RF-Nano** (Arduino Nano with onboard nRF24L01+), and compatible with any Arduino Uno / Nano with an external nRF24L01+ module.

Current firmware version: **v1.61.0**

---

## Features

- **Full Hayes AT command set** — `ATD`, `ATA`, `ATO`, `ATH`, `ATI`, `ATE`, `AT&F`, `AT&Z`, `AT&Y`
- **Three link modes** — Transparent pipe (no framing), Hardware ACK, or Software Flow Control (**default**)
- **Cooperative half-duplex** — automatic yield token in SWFLOW mode eliminates RF collisions during simultaneous bidirectional transfers; no configuration required
- **S-registers S0–S18** — all timing, retry, keep-alive, flow control, scanner, and silent-mode parameters; all saved to EEPROM with write-if-different (`EEPROM.update`)
- **XON/XOFF flow control** on both serial and radio links; 256-byte circular buffers
- **Keep-alive / ping-pong** with configurable interval and miss threshold; symmetric timeout
- **Channel busy detection** before dialling — configurable sample count
- **Spectrum analyser** — `ATSPECTRUM` sweeps all 126 channels with accurate RPD sampling; ASCII density display with ruler every 40 sweeps
- **Speed test** — `ATTEST-TX` / `ATTEST-RX` measure throughput and link quality with per-second stats including PDR, retransmit rate, and burst loss
- **Echo reflector** — `ATTEST-ECHO` bounces received packets back for human round-trip testing
- **Diagnostic ping** — `ATPINGXXXXXX` sends a round-trip probe to any idle node
- **Hardware reboot** — `ATREBOOT` triggers a clean watchdog reset
- **Silent mode (S18)** — suppresses all serial output; modem is completely invisible to the host while LEDs remain active
- **Retry forever (S8=255)** — retry dialling indefinitely; ideal for unattended wireless bridges where the remote may be absent for hours or days
- **Autodial on startup** — store up to 4 dial strings, set one to fire 2 s after boot
- **8 status LEDs** — MR, TR, OH, CD, SD, RD, HS, ER; usable as equipment logic signals
- **Configurable baud rate** 2400–1 000 000, saved to EEPROM

---

## Hardware

### RF-Nano (recommended)

| Signal | Pin |
|---|---|
| CE | D7 *(hardwired)* |
| CSN | D8 *(hardwired)* |
| MOSI | D11 |
| MISO | D12 |
| SCK | D13 |

### Arduino Uno / Nano + external nRF24L01+

Same pinout. Add a 10–100 µF capacitor across VCC/GND on the module to stabilise the 3.3 V supply.

### Status LEDs

Connect each LED via a **330 Ω resistor** to GND.

| LED | Pin | Colour | Meaning |
|---|---|---|---|
| **MR** Modem Ready | A0 | Green | Radio healthy |
| **TR** Terminal Ready | A1 | Green | Serial activity in last 200 ms |
| **OH** Off Hook | A2 | Yellow | Any non-idle state |
| **CD** Carrier Detect | A3 | Yellow | Connected — use as "link up" signal for equipment |
| **SD** Send Data | A4 | Red | Flashes on each TX packet |
| **RD** Receive Data | A5 | Red | Flashes on each RX packet |
| **HS** High Speed | D2 | Green | 2 Mbps air rate active |
| **ER** Error | D3 | Red | TX failure / NO CARRIER / data loss |

> LED outputs are unaffected by S18 silent mode — use them for equipment logic even when the modem is completely invisible to the serial terminal. **CD (A3)** is the standard "carrier detect" signal, suitable for driving external equipment logic to indicate link state.

---

## Dependencies

- [RF24 by TMRh20](https://github.com/nRF24/RF24) — install via Sketch → Include Library → Manage Libraries

---

## Getting Started

1. Flash the same sketch to both Arduinos.
2. Open a serial terminal at **115 200 baud**.
3. Set MACs:
   ```
   ATSMYMAC=A1B2C3    → OK
   ATSMYMAC=D4E5F6    → OK   (other node)
   ```
4. Optional auto-answer on node B: `ATS0=1`
5. Dial: `ATD D4E5F6` → `CONNECT`
6. Type freely. Use `+++` (1 s silence each side) to escape to command mode, `ATO` to return, `ATH` to disconnect.

---

## AT Command Reference

### Connection

| Command | Description |
|---|---|
| `ATD XXYYZZ` | Dial remote MAC. Returns `BUSY` if channel occupied and S14=1. |
| `ATA` | Manually answer an incoming call |
| `ATO` | Return to data mode after `+++` |
| `ATH` | Hang up (`NO CARRIER`) or reject ring (`OK`) |
| `ATS0=n` | Auto-answer after *n* rings (0 = off) |
| `+++` | Escape to command mode (1 s guard each side) |

### Configuration

| Command | Description |
|---|---|
| `ATI` | Full modem status — radio, state, MACs, RSSI, all registers, counters |
| `ATE0` / `ATE1` | Echo off / on |
| `ATSMYMAC=XXYYZZ` | Set own 3-byte MAC, saved to EEPROM |
| `ATSMYMAC?` | Query own MAC |
| `ATSETCH=nn` | Set RF channel 0–125 (default 97 = 2497 MHz) |
| `ATSETCH?` | Query RF channel |
| `ATSSPEED=n` | Air rate: `0`=250 kbps  `1`=1 Mbps (default)  `2`=2 Mbps |
| `ATSSPEED?` | Query air rate |
| `ATSBAUD=n` | Set serial baud rate; `OK` at old rate then port switches |
| `ATSBAUD?` | Query baud rate |
| `ATSFLOW=n` | Link mode: `0`=transparent  `1`=HWACK  `2`=SWFLOW (default) |
| `ATSFLOW?` | Query link mode |
| `AT&F` | Factory reset — restore defaults, rewrite EEPROM |
| `AT&Zn=string` | Store dial string in slot *n* (0–3). Empty = clear. |
| `AT&Zn?` | Query stored dial string |
| `AT&Yn` | Set startup autodial slot (fires 2 s after boot) |
| `AT&Y?` | Query startup slot |

### Diagnostics

| Command | Description |
|---|---|
| `ATSPECTRUM` | Sweep 126 channels, display ASCII spectrum. Any key stops. |
| `ATPINGXXYYZZ` | Ping MAC `XXYYZZ` when idle. Prints `PONG from XXXXXX` on reply. |
| `ATREBOOT` | Hardware reboot via AVR watchdog — boots fresh from EEPROM |
| `ATTEST-TX` | Speed test transmitter (requires connected + `+++`) |
| `ATTEST-RX` | Speed test receiver (requires connected + `+++`) |
| `ATTEST-ECHO` | Echo reflector — bounces received packets back (requires connected + `+++`) |

### S-Registers

All registers saved to EEPROM. Query: `ATSn?` (3-digit zero-padded). Set: `ATSn=value`.

| Reg | Default | Range | Description |
|---|---|---|---|
| S0 | 0 | 0–255 | Auto-answer ring count (0 = disabled) |
| S6 | 0 | 0–255 s | Pre-dial wait, non-blocking (0 = none) |
| S7 | 3 | 1–255 s | Carrier wait per dial attempt; also `ATPING` reply timeout |
| S8 | 3 | 0–255 | Dial retries after failure: `0`=none  `255`=forever  else count |
| S9 | 3 | 0–255 s | Inter-retry interval |
| S10 | 1 | 0–1 | Keep-alive: `1`=on (default)  `0`=off |
| S11 | 5 | 1–255 s | Keep-alive ping interval |
| S12 | 3 | 1–255 | Missed pings before `NO CARRIER` |
| S13 | 1 | 0–1 | XON/XOFF flow control: `1`=on (default)  `0`=off |
| S14 | 1 | 0–1 | Busy detect before dial: `1`=on (default)  `0`=off |
| S15 | 50 | 1–255 | Busy detect sample count (~2 ms each; default ≈ 100 ms scan) |
| S16 | 5 | 1–255 ms | Transparent mode TX idle flush timeout |
| S17 | 20 | 1–255 | Spectrum scan samples per channel (~500 µs each) |
| S18 | 0 | 0–1 | Silent mode: `1`=suppress all CLI serial output  `0`=normal |

> **S8=255 (retry forever):** TX modem retries indefinitely at `S7+S9` second intervals. Counter is `uint32_t` — no overflow for hundreds of years of retrying. Ideal for unattended bridges where the remote may be absent for days. `ATS18=0` always works to escape silent mode regardless of S18 value.

---

## Supported Baud Rates

2400 · 4800 · 9600 · 19200 · 38400 · 57600 · **115200** (default) · 250000 · 500000 · 1000000

> **Note:** if EEPROM was saved before the 2400/4800 rates were added the stored baud index may point to the wrong rate. Issue `ATSBAUD=115200` or `AT&F` to correct.

---

## Link Modes (`ATSFLOW`)

### SWFLOW — Software Flow Control (`ATSFLOW=2`, **default**)

Pure stop-and-wait ARQ. The sender transmits one `PKT_DATA` and waits up to 5 ms for `PKT_SWACK`. If no reply, retransmits up to 4 times. Only mode with cooperative half-duplex.

### HWACK — Hardware ACK (`ATSFLOW=1`)

Hardware acknowledgement via nRF24L01 built-in auto-ACK. Every packet confirmed before next is sent. Reliable on noisy links; throughput ~50 kbps. Cooperative duplex not available.

### Transparent (`ATSFLOW=0`)

No framing, no ACK, no protocol. `ATD` connects instantly. Every serial byte transmitted over radio; every radio byte forwarded to serial. Application handles all framing. Packets always 32 bytes (zero-padded after S16 ms idle).

**Maximum throughput:**
```
ATSSPEED=2   ATSFLOW=2   ATSBAUD=500000
```

---

## Cooperative Half-Duplex

The nRF24L01 cannot transmit and receive simultaneously. Without coordination, both sides sending at once causes silent data loss.

In SWFLOW mode every `PKT_SWACK` carries a **yield token**. When the receiver has data to send it replies with `PKT_SWACK_YIELD` instead of plain `PKT_SWACK`. The sender pauses and waits for the receiver's packet, then resumes. Both sides alternate packet-by-packet under full duplex load — no configuration, zero overhead when idle.

```
A ──[data]──────────────────────────────► B
A ◄─────────────────[SWACK_YIELD]──────── B  ← B has data
A  waits...
B ──[data]──────────────────────────────► A
B ◄─────────────────[SWACK_YIELD]──────── A  ← A still has data
A ──[data]──────────────────────────────► B
... alternates until one side drains
```

---

## Silent Mode (S18)

Setting `ATS18=1` suppresses all serial output — result codes, unsolicited messages, boot banner, ATI output. The modem becomes completely invisible to the connected device while continuing to operate normally.

**LEDs are unaffected** — all 8 LEDs continue to indicate state normally. Use **CD (A3)** as a hardware "link up" signal for equipment logic.

**Escape hatch:** `ATS18=0` always produces output and restores normal operation, even when silent. After `+++` (which gives no confirmation when silent), type `ATS18=0` to un-silence.

**Typical silent bridge setup:**
```
Both modems:
  ATE0               ← echo off
  ATS18=1            ← silent mode
  ATS8=255           ← retry forever (TX side)
  ATS0=1             ← auto-answer (RX side)
  AT&Y0              ← autodial on boot (TX side)
  AT&Z0=XXYYZZ       ← remote MAC (TX side)
  ATREBOOT           ← reboot to apply all settings
```

After reboot: TX boots silently, dials RX, connects, data flows. No bytes ever appear on either serial port.

---

## Retry Forever (S8=255)

With `S8=255` the TX modem retries the dial indefinitely at `S7 + S9` second intervals (default 6 s/cycle). The retry counter is a `uint32_t` — no overflow concern for any practical deployment.

This enables robust unattended wireless links where the remote may be absent for extended periods — a car driving away for days, a field device that powers down at night, etc. When the remote returns within range, the next handshake succeeds automatically and the connection is established without any intervention.

---

## Dial Sequence

```
ATD XXYYZZ
  → S14=1: scan channel S15 samples (~2 ms each) — BUSY if occupied
  → attempt 1: send PKT_CONN, wait S7 s for PKT_ACK
      reply → CONNECT
      timeout → NO CARRIER - retry N/M, wait S9 s
  → attempts 2 … (S8 or forever)
  → NO CARRIER  (final, only when S8 ≠ 255)
```

`RING` prints once per received `PKT_CONN`. Receiver returns to `S_IDLE` automatically once the caller's retry window expires (or after 1 hour if S8=255 on the caller).

---

## Keep-Alive

When `S10=1` the initiator sends `PKT_PING` every S11 s. The answerer always replies with `PKT_PONG`.

| Side | S10=1 | S10=0 |
|---|---|---|
| **Initiator** | Sends pings; `NO CARRIER` after S12 consecutive misses | No pings |
| **Answerer** | Replies to pings; `NO CARRIER` after `S11×S12` s silence | Replies; never times out |

Default: 5 s × 3 misses = 15 s window. Both sides disconnect simultaneously.

---

## Flow Control

XON/XOFF active by default (`S13=1`). XOFF sent at 75% buffer full (192/256 bytes), XON at 25% (64 bytes). Applied to both serial and radio links. Set `ATS13=0` if terminal doesn't honour XON/XOFF.

---

## Spectrum Analyser

```
ATSPECTRUM
```

Sweeps all 126 channels (2400–2525 MHz). S17 independent RPD measurements per channel — each a complete `stopListening` → `startListening` → 500 µs settle → read cycle for accurate latch-reset sampling. Density mapped to ASCII (`' '`=0% through `'%'`=100%). Ruler every 40 sweeps. Any key stops.

WiFi ch 1/6/11 appear as dense blocks near nRF24 channels 1/26/51. Default channel 97 (2497 MHz) is above all WiFi and Bluetooth.

---

## Speed Test

Requires an active connection — dial first, then `+++`.

**Setup:**
```
Node A: ATD → CONNECT → +++ → ATTEST-TX
Node B: ATA → CONNECT → +++ → ATTEST-RX
```

TX sends `PKT_TEST_START` then floods 29-byte sequenced packets (`seq[4] + magic 0xDEADBEEF[4] + flag[1] + fill[20]`). RX arms on the first packet containing the magic signature.

**Stats every second:**
```
[TX] t=5s  pkts=2060  speed=4120 Payload B/s  retx=3 (0%)  drop=0
[RX] t=5s  pkts=2058  speed=4116 Payload B/s  PDR=99%  lost=2  burst=1  dup=0  drop=0
```

**Quality metrics:**

| Metric | Side | Meaning |
|---|---|---|
| `retx (N%)` | TX | Packets needing retransmit. 0% = perfect. |
| `PDR%` | RX | Packet delivery ratio — ground truth of link quality |
| `lost` | RX | Sequence gaps — packets never arrived |
| `burst` | RX | Largest consecutive loss run — `1`=random noise, `>5`=structured interference |
| `dup` | RX | Duplicates — retransmit arrived when original also got through |
| `drop` | both | Buffer overflow bytes |

**Stopping:** any keypress sends a stop-flagged packet through the same reliable data path. Both sides exit and print a final summary.

**Echo mode:** `ATTEST-ECHO` reflects every received packet back to the sender — useful for human round-trip latency testing without a second terminal operator.

---

## Diagnostic Ping

```
ATPING D4E5F6
```

Idle-only (`S_IDLE` state). Sends `PKT_DIAG_PING` carrying own MAC, waits up to S7 s for `PKT_DIAG_PONG`.

```
Sender:   PONG from D4E5F6 / OK
Receiver: PING from A1B2C3
```

Uses dedicated packet types (0x0C/0x0D) — no interference with keep-alive.

---

## Hardware Reboot

```
ATREBOOT
```

Arms the AVR watchdog for 15 ms then loops. The chip resets, EEPROM settings reload, and the modem starts fresh — equivalent to pressing the reset button. Useful after `AT&F` to apply factory defaults without power-cycling.

---

## Multiple Pairs on the Same Channel

Pipe addresses use all 3 MAC bytes directly (`0xAB 0xCD [mac0] [mac1] [mac2]`) — no collision risk between distinct MACs. Packets for other pairs are filtered at the radio hardware level. Physical RF collisions only occur when two radios transmit simultaneously; probability is low under light traffic. For concurrent heavy use, assign pairs to separate channels with `ATSETCH`.

---

## Packet Protocol

32-byte fixed payload. Header: 3 bytes. User payload: up to 29 bytes.

| Type | Value | Description |
|---|---|---|
| PKT_DATA | 0x01 | User data |
| PKT_XON | 0x02 | Resume sending |
| PKT_XOFF | 0x03 | Pause sending |
| PKT_DISC | 0x04 | Disconnect (sent 3× with 10 ms gaps) |
| PKT_CONN | 0x05 | Connection request |
| PKT_ACK | 0x06 | Connection accepted |
| PKT_NACK | 0x07 | Reserved |
| PKT_SWACK | 0x08 | SWFLOW cumulative ACK |
| PKT_PING | 0x09 | Keep-alive ping |
| PKT_PONG | 0x0A | Keep-alive reply |
| PKT_SWACK_YIELD | 0x0B | SWFLOW ACK + yield TX to remote |
| PKT_DIAG_PING | 0x0C | Diagnostic ping (ATPING) |
| PKT_DIAG_PONG | 0x0D | Diagnostic pong |
| PKT_TEST_START | 0x0E | Speed test start / re-arm |
| PKT_TEST_STOP | 0x0F | Speed test stop |

---

## EEPROM Layout

94 bytes (0–93), magic byte `0xA5` at offset 8. All writes use `EEPROM.update()`.

| Offset | Size | Contents |
|---|---|---|
| 0–2 | 3 B | Own MAC |
| 3–5 | 3 B | Remote MAC |
| 6 | 1 B | RF channel |
| 7 | 1 B | Speed enum |
| 8 | 1 B | Magic (`0xA5`) |
| 9 | 1 B | S0 |
| 10 | 1 B | Flow mode |
| 11 | 1 B | Baud index (0–9) |
| 12–79 | 68 B | Dial strings ×4 |
| 80 | 1 B | Startup slot |
| 81–93 | 13 B | S6–S18 |

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| MR off, ER blinking at boot | nRF24L01 not detected | Check wiring; add 100 µF cap on VCC/GND |
| ER flashing during transfer | Buffer overflow | Enable XON/XOFF (`ATS13=1`) or reduce baud |
| BUSY right after disconnect | RPD latch not cleared | Normal — 200 ms grace period |
| NO CARRIER immediately | Remote not listening / wrong channel | Check `ATSETCH` matches |
| RING stops / NO ANSWER | Caller gave up | Increase S7/S8 or use `ATS0=1` |
| Garbled after ATSBAUD | Terminal not switched | Change terminal baud or reboot |
| Link drops when idle | KA timeout | Increase S11/S12 or `ATS10=0` |
| Binary data corrupt | XON/XOFF intercepting 0x11/0x13 | `ATS13=0` |
| No output after changes | S18=1 silent mode | Type `ATS18=0` (works even when silent) |
| Wrong baud after update | Stale EEPROM baud index | `ATSBAUD=115200` or `AT&F` |
| TX keeps retrying forever | S8=255 set | Intended — `ATH` to cancel |
| ATSPECTRUM shows nothing | RPD latch issue | Call from S_IDLE; increase S17 |

---

## Version History

| Version | Summary |
|---|---|
| v1.0–v1.10 | Initial build — AT commands, SWFLOW, keep-alive, flow control, spectrum, transparent |
| v1.11–v1.25 | S14–S18, ATSFLOW rename, RPD fixes, txPop wrap bug fix (root corruption cause) |
| v1.26–v1.35 | ATH/ATO, SWFLOW session reset, EEPROM.update, macToAddr 3-byte |
| v1.36–v1.39 | swflowAckData bool, SWFLOW full rewrite to stop-and-wait |
| v1.40 | 2400/4800 baud rates |
| v1.41–v1.45 | ATTEST-TX/RX speed test, link quality metrics (PDR, retx%, burst) |
| v1.46–v1.49 | Test stop reliability, KA timer reset on test exit |
| v1.50 | PKT_DISC sent 3× for reliable disconnect |
| v1.51–v1.52 | S_CONNECTED data discard fix (no serial leakage in CLI mode) |
| v1.53–v1.55 | Speed test payload byte counting, Payload B/s label |
| v1.56 | Stop packet detection inside flushTxBuffer ACK wait loop |
| v1.57 | ATTEST-ECHO echo reflector |
| v1.58–v1.59 | S18 silent mode |
| v1.60 | S8=255 retry forever, ATREBOOT watchdog reset |
| v1.61 | dialRetryCount promoted to uint32_t (no overflow in forever mode) |

---

## License

MIT — free to use, modify, and distribute. Attribution appreciated.

# nRF24L01 Hayes AT Modem

A full Hayes-compatible AT command modem emulator for Arduino, using the nRF24L01+ radio module as the wireless link. Connect two Arduinos and get a wireless serial pipe with proper modem semantics: dialling, answering, flow control, keep-alive, diagnostics, and status LEDs — or configure both as a completely silent, invisible wireless bridge.

Designed and tested on the **RF-Nano** (Arduino Nano with onboard nRF24L01+), and compatible with any Arduino Uno / Nano with an external nRF24L01+ module.

Current firmware version: **v1.74.0**

---

## Typical Session Example

![Typical session](images/session.jpg)

---

## Features

- **Full Hayes AT command set** — `ATD`, `ATA`, `ATO`, `ATH`, `ATI`, `ATE`, `ATRE`, `AT&F`, `AT&Z`, `AT&Y`
- **Three link modes** — Transparent pipe (no framing), Hardware ACK, or Software Flow Control (**default**)
- **Cooperative half-duplex** — automatic yield token in SWFLOW mode eliminates RF collisions during simultaneous bidirectional transfers; no configuration required
- **S-registers S0–S18** — all timing, retry, keep-alive, flow control, scanner, and silent-mode parameters; all saved to EEPROM with write-if-different (`EEPROM.update`)
- **XON/XOFF flow control** on both serial and radio links; 256-byte circular buffers
- **Keep-alive / ping-pong** with configurable interval and miss threshold; symmetric timeout; KA active in both `S_DATA` and `S_CONNECTED` states
- **Channel busy detection** before dialling — configurable sample count
- **Channel auto-select** — `ATSETCHAUTO` scans all 126 channels and picks the quietest one automatically
- **Spectrum analyser** — `ATSPECTRUM` sweeps all 126 channels with accurate RPD sampling; ASCII density display with ruler every 40 sweeps
- **Speed test** — `ATTEST-TX` / `ATTEST-RX` non-blocking throughput measurement integrated into normal `loop()` — KA stays operational during tests
- **Echo reflector** — `ATTEST-ECHO` bounces received packets back for round-trip testing
- **Diagnostic ping** — `ATPINGXXXXXX` sends a round-trip probe to any idle node
- **Hardware reboot** — `ATREBOOT` triggers a clean watchdog reset
- **Silent mode (S18)** — suppresses all serial output; modem invisible to host; LEDs unaffected
- **Retry forever (S8=255)** — retry dialling indefinitely; ideal for unattended wireless bridges
- **Connection uptime** — `ATI` shows how long the current connection has been established
- **Autodial on startup** — store up to 4 dial strings, set one to fire 2 s after boot
- **8 status LEDs** — MR, TR, OH, CD, SD, RD, HS, ER; CD usable as hardware "link up" signal
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
| **CD** Carrier Detect | A3 | Yellow | Connected — usable as "link up" signal for equipment |
| **SD** Send Data | A4 | Red | Flashes on each TX packet |
| **RD** Receive Data | A5 | Red | Flashes on each RX packet |
| **HS** High Speed | D2 | Green | 2 Mbps air rate active |
| **ER** Error | D3 | Red | TX failure / NO CARRIER / data loss |

> LEDs are unaffected by S18 silent mode. **CD (A3)** goes HIGH when connected and LOW on disconnect — wire directly to equipment logic for a hardware link-state signal.

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
| `ATRE` | Re-dial last number — equivalent to `ATD <last MAC>` |
| `ATS0=n` | Auto-answer after *n* rings (0 = off) |
| `+++` | Escape to command mode (1 s guard each side) |

### Configuration

| Command | Description |
|---|---|
| `ATI` | Full modem status — radio, uptime, state, MACs, RSSI, all registers, counters |
| `ATE0` / `ATE1` | Echo off / on |
| `ATSMYMAC=XXYYZZ` | Set own 3-byte MAC, saved to EEPROM |
| `ATSMYMAC?` | Query own MAC |
| `ATSETCH=nn` | Set RF channel 0–125 (default 97 = 2497 MHz) |
| `ATSETCH?` | Query RF channel |
| `ATSETCHAUTO` | Scan all 126 channels and auto-select the quietest (idle only) |
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
| `ATTEST-ECHO` | Echo reflector (requires connected + `+++`) |

### S-Registers

All saved to EEPROM. Query: `ATSn?` (3-digit zero-padded). Set: `ATSn=value`.

| Reg | Default | Range | Description |
|---|---|---|---|
| S0 | 0 | 0–255 | Auto-answer ring count (0 = disabled) |
| S6 | 0 | 0–255 s | Pre-dial wait, non-blocking (0 = none) |
| S7 | 3 | 1–255 s | Carrier wait per dial attempt; also `ATPING` reply timeout |
| S8 | 3 | 0–255 | Dial retries: `0`=none  `255`=forever  else count |
| S9 | 3 | 0–255 s | Inter-retry interval |
| S10 | 1 | 0–1 | Keep-alive: `1`=on (default)  `0`=off |
| S11 | 5 | 1–255 s | Keep-alive ping interval |
| S12 | 3 | 1–255 | Missed pings before `NO CARRIER` |
| S13 | 1 | 0–1 | XON/XOFF flow control: `1`=on (default)  `0`=off |
| S14 | 1 | 0–1 | Busy detect before dial: `1`=on (default)  `0`=off |
| S15 | 50 | 1–255 | Busy detect sample count (~2 ms each; default ≈ 100 ms scan) |
| S16 | 5 | 1–255 ms | Transparent mode TX idle flush timeout |
| S17 | 20 | 1–255 | Spectrum/auto-select samples per channel (~500 µs each) |
| S18 | 0 | 0–1 | Silent mode: `1`=suppress all CLI serial output  `0`=normal |

> **S8=255 (retry forever):** retries indefinitely at `S7+S9` second intervals. Counter is `uint32_t` — effectively no overflow. **ATS18=0** always produces output and works even when silent.

---

## Supported Baud Rates

2400 · 4800 · 9600 · 19200 · 38400 · 57600 · **115200** (default) · 250000 · 500000 · 1000000

> If EEPROM was saved before the 2400/4800 rates were added, the stored index may point to the wrong rate. Issue `ATSBAUD=115200` or `AT&F` to correct.

---

## Link Modes (`ATSFLOW`)

### SWFLOW — Software Flow Control (`ATSFLOW=2`, **default**)

Pure stop-and-wait ARQ. Sends one `PKT_DATA`, waits up to 5 ms for `PKT_SWACK`, retransmits up to 4 times on no reply. Only mode with cooperative half-duplex.

### HWACK — Hardware ACK (`ATSFLOW=1`)

Hardware acknowledgement via nRF24L01 built-in auto-ACK. Every packet confirmed before next is sent. ~50 kbps throughput. No cooperative duplex.

### Transparent (`ATSFLOW=0`)

No framing, no ACK, no protocol. `ATD` connects instantly. Serial bytes → radio; radio bytes → serial. Application handles all framing. Packets always 32 bytes (zero-padded after S16 ms idle).

**Maximum throughput:** `ATSSPEED=2` + `ATSFLOW=2` + `ATSBAUD=500000`

---

## Cooperative Half-Duplex

The nRF24L01 cannot transmit and receive simultaneously. In SWFLOW mode every `PKT_SWACK` carries a **yield token** — when the receiver has data to send it replies with `PKT_SWACK_YIELD`, pausing the sender and giving itself a transmission window. Both sides alternate packet-by-packet under full duplex load. No configuration, zero overhead when idle.

KA ping/pong packets are handled **outside** the yield mechanism — they fire directly and get a dedicated 20 ms listen window to ensure replies are received even during heavy data flooding.

```
A ──[data]────────────────────────────────► B
A ◄──────────────────[SWACK_YIELD]───────── B  ← B has data
A  waits...
B ──[data]────────────────────────────────► A
B ◄──────────────────[SWACK_YIELD]───────── A  ← A still has data
... alternates until one side drains
```

---

## Silent Mode (S18)

`ATS18=1` suppresses all serial output — result codes, unsolicited messages, boot banner, ATI. Modem is completely invisible to the host. LEDs continue normally. `ATS18=0` always works to restore output. After `+++` (no confirmation when silent), type `ATS18=0` blind to un-silence.

**Typical silent bridge setup:**
```
Both modems:    ATE0, ATS18=1
TX modem:       ATS8=255, AT&Z0=XXYYZZ, AT&Y0, ATREBOOT
RX modem:       ATS0=1, ATREBOOT
```

After reboot: TX boots silently, dials forever, connects when RX is in range. No bytes on either serial port. CD (A3) HIGH = link up.

---

## Channel Auto-Select

```
ATSETCHAUTO
```

Scans all 126 channels (idle only), performs S17 independent RPD measurements per channel, selects the one with fewest hits, sets it as the active channel, and saves to EEPROM. Prints progress dots then the result:

```
Scanning channels.......
Best channel: 42 (0/20 hits — saved)
```

Increase S17 for more reliable selection at the cost of scan time. Default 20 samples ≈ 1.3 s total scan.

---

## Speed Test

Requires an active connection — dial first, then `+++`. Non-blocking: KA pings continue during the test, cooperative half-duplex remains active.

```
Node A:  ATD → CONNECT → +++ → ATTEST-TX
Node B:  ATA → CONNECT → +++ → ATTEST-RX
```

TX sends `Test123_NNNNNNNNNN\r\n` packets (29 bytes each, counter increments every packet). Any keypress stops either side.

**Stats every second (instantaneous throughput):**
```
[TX] t=5s  pkts=2060  speed=4120 B/s  retx=3 (0%)  drop=0
[RX] t=5s  pkts=2058  speed=4116 B/s  drop=0
```

`ATTEST-ECHO` reflects received packets back to sender — useful for human round-trip testing.

---

## Diagnostic Ping

```
ATPING D4E5F6
```

Idle-only. Sends `PKT_DIAG_PING` to target MAC, waits up to S7 s for reply.

```
Sender:   PONG from D4E5F6  /  OK
Target:   PING from A1B2C3
```

Uses dedicated packet types (0x0C/0x0D) — no interference with keep-alive.

---

## Keep-Alive

KA operates in both `S_DATA` and `S_CONNECTED` states. When transitioning between them (`+++`, `ATO`, test start/stop), `kaResetWindow()` gives a fresh full window to prevent false disconnects.

When `S10=1` the initiator sends `PKT_PING` every S11 s. The answerer always replies with `PKT_PONG`.

| Side | S10=1 | S10=0 |
|---|---|---|
| **Initiator** | Sends pings; `NO CARRIER` after S12 consecutive misses | No pings |
| **Answerer** | Replies to pings; `NO CARRIER` after `S11×S12` s silence | Replies; never times out |

Default: 5 s × 3 misses = 15 s window on each side.

---

## Dial Sequence

```
ATD XXYYZZ
  → S14=1: scan channel S15 samples — BUSY if occupied
  → attempt 1: send PKT_CONN, wait S7 s for PKT_ACK
      reply → CONNECT
      timeout → NO CARRIER - retry N/M (or N/forever), wait S9 s
  → attempts 2 … (S8 or forever)
  → NO CARRIER  (final, only when S8 ≠ 255)
```

`ATRE` re-dials the last number without retyping the MAC.

---

## ATI Output

When connected, `ATI` includes connection uptime:

```
nRF24L01 AT Modem v1.74.0
Radio   : OK
Uptime  : 1h 23m 45s
State   : CONNECTED (data mode)
...
```

---

## Multiple Pairs on the Same Channel

Pipe addresses use all 3 MAC bytes directly — no collision risk between distinct MACs. Physical RF collisions only occur when two radios transmit simultaneously; probability is low under light traffic. For concurrent heavy use assign pairs to separate channels with `ATSETCH` or `ATSETCHAUTO`.

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
| PKT_TEST_START | 0x0E | Reserved |
| PKT_TEST_STOP | 0x0F | Reserved |

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
| BUSY right after disconnect | RPD latch grace period | Normal — wait ~200 ms |
| NO CARRIER immediately | Remote not listening / wrong channel | Check `ATSETCH` matches; try `ATSETCHAUTO` |
| RING stops / NO ANSWER | Caller gave up | Increase S7/S8 or use `ATS0=1` |
| Garbled after ATSBAUD | Terminal not switched | Change terminal baud or reboot |
| Link drops when idle | KA timeout | Increase S11/S12 or `ATS10=0` |
| Binary data corrupt | XON/XOFF intercepting 0x11/0x13 | `ATS13=0` |
| No output after changes | S18=1 silent mode | Type `ATS18=0` (always works) |
| Wrong baud after update | Stale EEPROM baud index | `ATSBAUD=115200` or `AT&F` |
| Test drops NO CARRIER ~18s | KA stale state on test start | Fixed in v1.70+ — update firmware |
| ATSETCHAUTO returns ERROR | Not in S_IDLE | Disconnect first (`ATH`) |

---

## Version History

| Version | Summary |
|---|---|
| v1.0–v1.25 | Initial build through core bug fixes (txPop wrap, SWFLOW session reset) |
| v1.26–v1.39 | ATH/ATO/ATRE, EEPROM.update, macToAddr 3-byte, SWFLOW full rewrite to stop-and-wait |
| v1.40 | 2400/4800 baud rates |
| v1.41–v1.52 | Speed test, link quality metrics, S_CONNECTED data discard fix |
| v1.53–v1.57 | Payload B/s, test stop reliability, ATTEST-ECHO |
| v1.58–v1.61 | S18 silent mode, S8=255 forever, ATREBOOT, dialRetryCount uint32_t |
| v1.62–v1.65 | Test rewrite (non-magic pattern+counter), KA state fixes |
| v1.66–v1.67 | PKT_DATA KA watchdog refresh, non-blocking test loop integrated into loop() |
| v1.68 | Transparent mode TX test fix |
| v1.69 | Test flags cleared on NO CARRIER |
| v1.70 | `kaResetWindow()` — unified KA reset on all S_DATA↔S_CONNECTED transitions |
| v1.71 | RX test packet counter direct from radio |
| v1.72 | PKT_PING/PONG handled immediately in flushTxBuffer ACK wait loop |
| v1.73 | 20 ms pong listen window after sending PKT_PING during flooding |
| v1.74 | Uptime in ATI, ATRE re-dial, ATSETCHAUTO channel auto-select |

---

## License

MIT — free to use, modify, and distribute. Attribution appreciated.

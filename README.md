# nRF24L01 Hayes AT Modem

A full Hayes-compatible AT command modem emulator for Arduino, using the nRF24L01+ radio module as the wireless link. Drop-in compatible with any serial terminal that speaks AT commands — connect two Arduinos and get a wireless serial pipe with proper modem semantics: dialling, answering, flow control, keep-alive, and status LEDs.

Designed and tested on the **RF-Nano** (Arduino Nano with onboard nRF24L01+), and compatible with any Arduino Uno / Nano paired with an external nRF24L01+ module.

Current firmware version: **v1.45.0**

---

## Features

- **Full Hayes AT command set** — `ATD`, `ATA`, `ATO`, `ATH`, `ATI`, `ATE`, `AT&F`, `AT&Z`, `AT&Y`
- **Three link modes** — Transparent pipe (no framing), Hardware ACK, or Software Flow Control (**default**)
- **Cooperative half-duplex** — automatic yield token mechanism in SWFLOW mode gives each side a guaranteed clean transmit window during simultaneous bidirectional transfers, eliminating RF collisions without any configuration
- **S-registers S0–S17** covering all timing, retry, keep-alive, flow control, and scanner parameters — all saved to EEPROM with write-if-different (`EEPROM.update`)
- **XON/XOFF flow control** on both serial and radio links (S13, default ON); 256-byte circular buffers with 75%/25% thresholds
- **Keep-alive / ping-pong** with configurable interval and miss threshold (S10–S12); symmetric timeout so both sides disconnect simultaneously
- **Channel busy detection** before dialling — configurable sample count (S14, S15)
- **Spectrum analyser** — `ATSPECTRUM` sweeps all 126 channels with accurate RPD sampling and renders an ASCII signal density display with ruler footers every 40 sweeps
- **Speed test** — `ATTEST-TX` / `ATTEST-RX` measure real throughput and link quality with per-second stats including PDR, retransmit rate, and burst loss detection
- **Diagnostic ping** — `ATPINGXXXXXX` sends a round-trip probe to any idle node and reports response time
- **Autodial on startup** — store up to 4 dial strings, set one to fire 2 s after boot
- **8 status LEDs** mimicking a real modem front panel (MR, TR, OH, CD, SD, RD, HS, ER)
- **Radio health monitoring** — detects disconnected nRF24L01 at runtime and on boot
- **Data loss counters** — tracks dropped bytes in both directions, reported by `ATI`
- **Configurable baud rate** 2400–1 000 000, saved to EEPROM
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
| **SD** Send Data | A4 | Red | Flashes on each TX packet |
| **RD** Receive Data | A5 | Red | Flashes on each RX packet |
| **HS** High Speed | D2 | Green | 2 Mbps air rate active |
| **ER** Error | D3 | Red | TX failure / NO CARRIER / data loss |

> **MR** goes dark and **ER** blinks continuously if the nRF24L01 fails to initialise or disconnects at runtime.

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
6. Type freely — data flows both ways. Use `+++` (1 s silence each side) to return to command mode without hanging up, then `ATO` to return to data mode, or `ATH` to disconnect.

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
| `ATSBAUD=n` | Set serial baud rate. `OK` sent at old rate then port switches — match your terminal. |
| `ATSBAUD?` | Query current baud rate |
| `ATSFLOW=n` | Set link mode: `0`=transparent  `1`=HWACK  `2`=SWFLOW (**default**) |
| `ATSFLOW?` | Query link mode |
| `AT&F` | Factory reset — restores all defaults, rewrites EEPROM |

### Stored Dial Strings & Autodial

| Command | Description |
|---|---|
| `AT&Zn=string` | Store dial string in slot *n* (0–3). Empty string clears slot. |
| `AT&Zn?` | Query stored string in slot *n* |
| `AT&Yn` | Set startup autodial slot (fires 2 s after boot) |
| `AT&Y?` | Query startup slot (`none` if disabled) |

### Diagnostics

| Command | Description |
|---|---|
| `ATSPECTRUM` | Sweep all 126 RF channels and display ASCII spectrum density. Any key stops. |
| `ATPINGXXYYZZ` | Send diagnostic ping to MAC `XXYYZZ` when idle. Prints `PONG from XXXXXX` on reply. |
| `ATTEST-TX` | Start speed test transmitter (requires active connection + `+++` first) |
| `ATTEST-RX` | Start speed test receiver (requires active connection + `+++` first) |

### S-Registers

Query with `ATSn?` (returns zero-padded 3 digits). Set with `ATSn=value`. All saved to EEPROM.

| Register | Default | Range | Description |
|---|---|---|---|
| S0 | 0 | 0–255 | Auto-answer ring count (0 = disabled) |
| S6 | 0 | 0–255 s | Pre-dial wait, non-blocking (0 = none) |
| S7 | 3 | 1–255 s | Carrier wait per dial attempt; also ping timeout |
| S8 | 3 | 0–255 | Dial retry attempts after first failure (0 = no retry) |
| S9 | 3 | 0–255 s | Inter-retry interval |
| S10 | 1 | 0–1 | Keep-alive enable (1 = on) |
| S11 | 5 | 1–255 s | Keep-alive ping interval |
| S12 | 3 | 1–255 | Missed pings before `NO CARRIER` |
| S13 | 1 | 0–1 | Serial/radio flow control: `0`=none  `1`=XON/XOFF |
| S14 | 1 | 0–1 | Busy detect before dial: `0`=off  `1`=on |
| S15 | 50 | 1–255 | Busy detect sample count (each ~2 ms; default ≈ 100 ms scan) |
| S16 | 5 | 1–255 ms | Transparent mode TX idle flush timeout |
| S17 | 20 | 1–255 | Spectrum scan samples per channel (each ~500 µs) |

> With default S7=3, S8=3, S9=3: total dial window = 4 attempts × 3 s + 3 gaps × 3 s = **21 seconds** before final `NO CARRIER`. `RING` prints once per received `PKT_CONN` and stops automatically when the caller gives up.

---

## Supported Baud Rates

| Rate | Notes |
|---|---|
| 2400 | Very low — radio heavily starved |
| 4800 | Low; useful for slow legacy devices |
| 9600 | Minimum practical rate |
| 19200 | |
| 38400 | |
| 57600 | Well matched to HWACK mode (~50 kbps) |
| **115200** | **Default** |
| 250000 | Exceeds radio ceiling; serial no longer the bottleneck |
| 500000 | Recommended for maximum throughput |
| 1000000 | Works on CH340G; verify with your USB-serial chip |

> **Note:** if you previously saved a baud index before the 2400/4800 rates were added, the stored index will point to the wrong rate. Issue `ATSBAUD=115200` or `AT&F` to correct.

---

## Link Modes (`ATSFLOW`)

### SWFLOW — Software Flow Control (`ATSFLOW=2`, **default**)

Pure stop-and-wait ARQ. The sender transmits one `PKT_DATA` packet and waits up to 5 ms (`SW_ACK_WAIT_MS`) for a `PKT_SWACK` reply. If no reply arrives it retransmits up to 4 times (`SW_RETX_MAX`). This is the only mode that supports cooperative half-duplex (see below).

### HWACK — Hardware ACK (`ATSFLOW=1`)

Hardware acknowledgement via the nRF24L01's built-in auto-ACK and retry mechanism. Every 32-byte packet is confirmed by the radio hardware before the next is sent. Reliable on noisy links; throughput limited to ~50 kbps by ACK turnaround. Cooperative duplex is **not available** in this mode.

### Transparent (`ATSFLOW=0`)

No framing, no ACK, no protocol. `ATD` connects instantly without any handshake. Every byte from serial is transmitted over radio; every received radio byte is forwarded to serial. The application handles all framing, checksums, and flow control. Packets are always 32 bytes — zero-padded if the TX buffer doesn't fill a complete packet within S16 ms. `+++` and `ATH` still work.

**Maximum throughput settings:**
```
ATSSPEED=2        (2 Mbps air rate)
ATSFLOW=2         (SWFLOW — default)
ATSBAUD=500000    (serial well above radio ceiling)
```

---

## Cooperative Half-Duplex

The nRF24L01 is a **half-duplex radio** — it physically cannot transmit and receive at the same time. Without coordination, both sides transmitting simultaneously causes RF collisions where both packets are lost and neither side gets anything.

### The Problem

```
Without coordination — both sides lose:

A ──[data]──────────────► COLLISION ◄──────────[data]── B
            (both packets destroyed, nobody receives anything)
```

### The Solution — Yield Token

In SWFLOW mode, every software ACK packet (`PKT_SWACK`) carries a **yield token** that coordinates who transmits next. The mechanism is fully automatic — no configuration, no extra commands.

When side B receives a data packet from A and ACKs it:

- **B has nothing to send** → sends plain `PKT_SWACK`. A continues transmitting uninterrupted.
- **B has data queued** → sends `PKT_SWACK_YIELD`. A sees the yield flag, pauses, and actively waits for B's packet to arrive before resuming.

B then sends its packet and ACKs back — if A still has data it yields back, and the token alternates packet by packet for as long as both sides have data to send.

```
Both sides sending simultaneously:

A ──[data seq=N]────────────────────────────────► B
A ◄──────────────────────[SWACK_YIELD seq=N]──── B  ← B has data, requests TX window
A  pauses and waits...
B ──[data seq=M]────────────────────────────────► A
B ◄──────────────────────[SWACK_YIELD seq=M]──── A  ← A still has data, yields back
A ──[data seq=N+1]──────────────────────────────► B
... alternates packet-by-packet until one side's buffer drains
```

### Key Properties

No configuration required — always active in SWFLOW mode and completely transparent to the application. Under one-way traffic the yield path is never taken and there is zero performance overhead. Under full bidirectional load both sides get equal access to the channel with no long-term starvation. This was verified with simultaneous keyboard input from both sides producing clean alternating output with no corruption or domination by either side.

---

## Dial Sequence

```
ATD XXYYZZ
  → S14=1: scan channel for S15 samples (~2 ms each) — return BUSY if occupied
  → attempt 1: send PKT_CONN, wait S7 s for PKT_ACK
      PKT_ACK received → CONNECT
      S7 timeout → NO CARRIER - retry 1/S8, wait S9 s
  → attempts 2 … S8+1
  → NO CARRIER  (final)
```

On the receiver, `RING` prints once per received `PKT_CONN` packet (not on a timer). The receiver returns to `S_IDLE` automatically once the caller's retry window expires.

---

## Keep-Alive

When `S10=1` the **initiator** (dialling side) sends `PKT_PING` every S11 seconds. The **answerer** always replies with `PKT_PONG`.

| Side | S10=1 | S10=0 |
|---|---|---|
| **Initiator** | Sends pings; drops after S12 consecutive misses | No pings sent |
| **Answerer** | Replies to all pings; drops after `S11 × S12` s silence | Replies to all pings; never drops due to silence |

Default window: 5 s × 3 misses = 15 s — both sides disconnect at approximately the same time.

---

## Flow Control

XON/XOFF is active by default (`S13=1`). The modem sends `0x13` (XOFF) when its RX buffer exceeds 75% (192 of 256 bytes) and `0x11` (XON) when it drains below 25% (64 bytes). The same thresholds apply over the radio link.

Set `ATS13=0` if your terminal or application does not honour XON/XOFF — `0x11`/`0x13` bytes will then pass through as plain data. Buffer overflow is tracked and reported by `ATI`.

---

## Spectrum Analyser

```
ATSPECTRUM
```

Sweeps all 126 nRF24L01 channels (2400–2525 MHz). For each channel it performs S17 independent RPD measurements. Each measurement does a complete `stopListening` → `startListening` → 500 µs settle → `testRPD` cycle — this guarantees the RPD latch is properly reset between samples, giving accurate independent readings rather than stale latch values.

Hit percentage is mapped to an ASCII density character:

```
' ' 0%   '.' 1–12%   ':' 13–25%   '-' 26–37%   '=' 38–50%
'+' 51–62%   '*' 63–75%   '#' 76–87%   '@' 88–99%   '%' 100%
```

A channel ruler is printed at the start and repeated every 40 sweeps so the scale is always visible. Any keypress stops the scan. Tune S17 for faster (lower) or more reliable (higher) readings — at the default of 20 samples each sweep takes approximately 1.3 seconds.

WiFi channels 1, 6, 11 appear as dense blocks around nRF24 channels 1, 26, 51. The default operating channel 97 (2497 MHz) sits above all WiFi and Bluetooth activity.

```
Ch:  0         1         2         3   ...
     0123456789012345678901234567890123 ...
     @@@@@@@@@@@@@@@@@@@@@@@@@+:.      @@@@@@@@@@@@@@@@@@@@@@@@@+:.      @@@@@@@@@@@@@@@@
     ← WiFi ch1 ─────────────┘        ← WiFi ch6 ─────────────┘        ← WiFi ch11 ──
```

---

## Speed Test

The speed test measures real-world throughput and link quality between two connected nodes. It requires an established connection — dial first, then escape with `+++` before running the test commands.

### Setup

```
Node A (TX side):                    Node B (RX side):
  ATD 000002  → CONNECT               ATA  → CONNECT
  +++  → OK                           +++  → OK
  ATTEST-TX                           ATTEST-RX
```

The TX side immediately sends `PKT_TEST_START` to signal RX, then begins flooding test packets. The RX side waits silently until it sees the start signal and the first test packet containing the magic signature `0xDEADBEEF`, then arms and begins counting.

### Test Packet Format

Each packet carries a 4-byte sequence number (little-endian) at bytes 0–3, the magic signature `0xDEADBEEF` at bytes 4–7, and `0xAA` fill at bytes 8–28. The sequence number is used to detect gaps, duplicates, and burst losses exactly.

### Stats Output (every second)

**TX side:**
```
[TX] t=5s  pkts=2060  speed=5943 B/s  retx=3 (0%)  drop=0
```

**RX side:**
```
[RX] t=5s  pkts=2058  speed=5937 B/s  PDR=99%  lost=2  burst=1  dup=1  drop=0
```

### Quality Metrics Explained

| Metric | Side | Meaning |
|---|---|---|
| `retx (N%)` | TX | Packets that needed at least one retransmit. 0% = perfect link. |
| `PDR%` | RX | Packet Delivery Ratio — `received / (received + lost) × 100`. The ground truth of link quality. |
| `lost` | RX | Gaps in the sequence stream — packets that never arrived at all. |
| `burst` | RX | Largest single consecutive loss run. `burst=1` = isolated noise; `burst=10` = structured interference or obstruction. |
| `dup` | RX | Duplicates — TX retransmitted and both copies arrived. High dup with low retx% indicates SWACKs are being lost more than data. |
| `drop` | both | Buffer overflow bytes — indicates serial port can't keep up. |

### Interpreting Results

A perfect link shows `retx=0%`, `PDR=100%`, `lost=0`, `burst=0`. As the link degrades: isolated noise raises `retx%` first; physical obstruction raises `burst`; heavy interference raises both `lost` and `burst` simultaneously. Comparing `retx%` (TX view) with `100-PDR%` (RX view) shows whether retransmits are helping — if `retx=5%` but `PDR=100%`, retransmits are recovering all losses successfully.

### Stopping the Test

Press any key on either side. The stopping side sends `PKT_TEST_STOP` to the other side, which then also exits and prints its final summary. If TX is restarted without stopping RX first, TX sends `PKT_TEST_START` which causes RX to re-arm cleanly with all counters reset.

---

## Diagnostic Ping

```
ATPING D4E5F6
```

Available only from `S_IDLE` (no active or pending connection). Sends a `PKT_DIAG_PING` packet to the target MAC carrying the sender's own MAC as payload, then waits up to S7 seconds for a `PKT_DIAG_PONG` reply.

**Sender sees:**
```
PONG from D4E5F6
OK
```

**Target sees (if idle):**
```
PING from A1B2C3
```

The target automatically replies with its own MAC in the pong payload. Uses dedicated packet types (`PKT_DIAG_PING=0x0C`, `PKT_DIAG_PONG=0x0D`) completely separate from the keep-alive `PKT_PING`/`PKT_PONG` — the KA state machine is never touched. The target only responds when in `S_IDLE` — connected nodes silently ignore diagnostic pings.

---

## Multiple Pairs on the Same Channel

Two independent pairs can share a channel with minimal mutual interference. The nRF24L01 pipe address filtering discards packets addressed to other nodes at the software level. Physical RF collisions can occur when two radios transmit at exactly the same moment, but the probability is low when traffic is light.

With Pair 1 idle (keep-alive only: ~2 short packets every 5 s) and Pair 2 connecting or transferring light data, collision probability on any given Pair 1 packet is well under 0.1% — negligible in practice. Under heavy simultaneous load from both pairs, throughput on each pair degrades due to collisions. For demanding concurrent use, assign pairs to separate channels with `ATSETCH`.

Pipe addresses use all 3 MAC bytes directly (`0xAB 0xCD [mac0] [mac1] [mac2]`) ensuring no address collisions between any two distinct MACs.

---

## Packet Protocol

All packets are fixed 32 bytes (nRF24L01 payload size). Header: 3 bytes. User payload: up to 29 bytes.

```
Byte 0  — packet type
Byte 1  — sequence number (0–255, rolling)
Byte 2  — payload length (0–29)
Bytes 3–31 — payload
```

| Type | Value | Description |
|---|---|---|
| PKT_DATA | 0x01 | User data |
| PKT_XON | 0x02 | Resume sending (flow control) |
| PKT_XOFF | 0x03 | Pause sending (flow control) |
| PKT_DISC | 0x04 | Disconnect |
| PKT_CONN | 0x05 | Connection request (carries caller MAC) |
| PKT_ACK | 0x06 | Connection accepted |
| PKT_NACK | 0x07 | Reserved (not used in stop-and-wait mode) |
| PKT_SWACK | 0x08 | SWFLOW: cumulative ACK |
| PKT_PING | 0x09 | Keep-alive ping |
| PKT_PONG | 0x0A | Keep-alive reply |
| PKT_SWACK_YIELD | 0x0B | SWFLOW: ACK + yield TX window to remote |
| PKT_DIAG_PING | 0x0C | Diagnostic ping (ATPING command, idle-only) |
| PKT_DIAG_PONG | 0x0D | Diagnostic pong (reply to PKT_DIAG_PING) |
| PKT_TEST_START | 0x0E | Speed test start / re-arm signal |
| PKT_TEST_STOP | 0x0F | Speed test stop (from either side) |

---

## EEPROM Layout

93 bytes used (addresses 0–92), magic byte `0xA5` at offset 8. All writes use `EEPROM.update()` — bytes are only physically written when the value changes, protecting the rated 100,000-cycle lifetime.

| Offset | Size | Contents |
|---|---|---|
| 0–2 | 3 B | Own MAC |
| 3–5 | 3 B | Remote MAC (last dialled) |
| 6 | 1 B | RF channel |
| 7 | 1 B | Speed enum (0/1/2) |
| 8 | 1 B | Magic byte (`0xA5`) |
| 9 | 1 B | S0 |
| 10 | 1 B | Flow mode (ATSFLOW) |
| 11 | 1 B | Baud index (0–9) |
| 12–79 | 68 B | Dial strings × 4 (17 bytes each incl. NUL) |
| 80 | 1 B | Startup autodial slot |
| 81–92 | 12 B | S6–S17 |

If the magic byte is invalid on boot, all settings stay at compile-time defaults and EEPROM is left untouched until the first `saveConfig()` call.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| **MR off, ER blinking at boot** | nRF24L01 not detected | Check wiring; add 10–100 µF cap on VCC/GND |
| **ER flashing during transfer** | Buffer overflow | Enable XON/XOFF (`ATS13=1`) or reduce baud |
| **BUSY on ATD right after disconnect** | RPD latch not cleared | Normal — 200 ms grace period; wait briefly |
| **NO CARRIER immediately on ATD** | Remote not listening, wrong channel | Check `ATSETCH` matches; verify remote is idle |
| **RING stops / NO ANSWER printed** | Caller gave up before ATA | Increase S7 or S8; use auto-answer (`ATS0=1`) |
| **Garbled output after ATSBAUD** | Terminal not switched | Change terminal baud to match, or reboot |
| **Connection drops when idle** | Keep-alive timeout | Increase S11/S12, or disable with `ATS10=0` |
| **Binary transfers corrupt** | XON/XOFF intercepting 0x11/0x13 | Set `ATS13=0` |
| **ATSPECTRUM shows no activity** | RPD latch issue or wrong state | Call from S_IDLE; try increasing S17 |
| **ATTEST-RX won't arm** | RX started before TX, no START received | Stop and restart RX after TX is running |
| **ATPING returns NO RESPONSE** | Target not idle, wrong MAC, or wrong channel | Verify target is in S_IDLE and on same channel |
| **Wrong baud after firmware update** | Stale EEPROM baud index | Issue `ATSBAUD=115200` or `AT&F` |

---

## Version History

| Version | Summary |
|---|---|
| v1.0 | Initial release — basic AT commands, SWFLOW, keep-alive |
| v1.1–v1.10 | S-register expansion, transparent mode, busy detect, spectrum scan |
| v1.11–v1.20 | S14–S17, ATSFLOW rename (was ATSHWACK), RPD latch fixes |
| v1.21–v1.25 | **txPop wrap bug fix** (root cause of all data corruption), RING on PKT_CONN |
| v1.26–v1.30 | ATH result codes, ATO command, SWFLOW session reset on reconnect |
| v1.31 | `EEPROM.update`, parseMac cleanup, non-blocking S6, full 3-byte MAC addresses |
| v1.32 | Spectrum footer ruler every 40 sweeps |
| v1.33 | Spectrum scan rewrite — per-sample stop/start/settle, percentage density |
| v1.34 | Cooperative yield active-wait fix |
| v1.35–v1.37 | Window size reduction, duplicate/gap fix, fairness counter experiments |
| v1.38 | **Full SWFLOW rewrite** — pure stop-and-wait; window/NACK/gap-detection removed |
| v1.39 | First confirmed clean bidirectional duplex build |
| v1.40 | Added 2400 and 4800 baud rates |
| v1.41 | `ATTEST-TX` / `ATTEST-RX` speed test commands |
| v1.42 | Duplicate packet counter (`dup`) in RX test stats |
| v1.43 | `ATPINGXXXXXX` diagnostic ping command |
| v1.44 | `PKT_TEST_START` / `PKT_TEST_STOP` — coordinated test stop and re-arm |
| v1.45 | Link quality metrics in test stats: retransmit%, PDR%, burst loss |

---

## License

MIT — free to use, modify, and distribute. Attribution appreciated.

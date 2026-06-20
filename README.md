\# nRF24L01 Hayes AT Modem



A full Hayes-compatible AT command modem emulator for Arduino, using the nRF24L01+ radio module as the wireless link. Drop-in compatible with any serial terminal that speaks AT commands — connect two Arduinos and get a wireless serial pipe with proper modem semantics: dialling, answering, flow control, keep-alive, and status LEDs.



Designed and tested on the \*\*RF-Nano\*\* (Arduino Nano with onboard nRF24L01+), and compatible with any Arduino Uno / Nano paired with an external nRF24L01+ module.



\---



\## Features



\- \*\*Full Hayes AT command set\*\* — `ATD`, `ATA`, `ATH`, `ATI`, `ATE`, `AT\&F`, `AT\&Z`, `AT\&Y`

\- \*\*S-registers\*\* S0–S13 for all timing, retry, keep-alive and flow control parameters, all saved to EEPROM

\- \*\*Two link modes\*\* — Hardware ACK (reliable, \~50 kbps) or Software Flow Control (faster, \~80–100 kbps)

\- \*\*XON/XOFF flow control\*\* on both serial and radio links (S13, default ON)

\- \*\*Keep-alive / ping-pong\*\* with configurable interval and miss threshold (S10–S12)

\- \*\*Autodial on startup\*\* — store up to 4 dial strings, set one to fire 2 s after boot

\- \*\*8 status LEDs\*\* mimicking a real modem front panel (MR, TR, OH, CD, SD, RD, HS, ER)

\- \*\*Radio health monitoring\*\* — detects disconnected nRF24L01 at runtime and on boot

\- \*\*Data loss counters\*\* — tracks dropped bytes in both directions, reported by `ATI`

\- \*\*Configurable baud rate\*\* 9600–1 000 000, saved to EEPROM (`ATSBAUD`)

\- \*\*Factory reset\*\* (`AT\&F`) restores all defaults and overwrites EEPROM



\---



\## Hardware



\### RF-Nano (recommended)



The RF-Nano has the nRF24L01+ soldered directly onto the board. No external wiring needed for the radio — just add LEDs.



| nRF24L01+ signal | Arduino pin |

|---|---|

| CE | D7 \*(hardwired on RF-Nano)\* |

| CSN | D8 \*(hardwired on RF-Nano)\* |

| MOSI | D11 |

| MISO | D12 |

| SCK | D13 |



\### Arduino Uno / Nano + external nRF24L01+



Wire the module as above but use D7 → CE and D8 → CSN (or change `CE\_PIN` / `CSN\_PIN` in the sketch). Add a 10–100 µF capacitor across the module's VCC/GND to stabilise the 3.3 V supply.



\### Status LEDs



Connect each LED via a \*\*330 Ω resistor\*\* to GND (anode to pin, cathode via resistor to GND).



| LED | Pin | Colour | Meaning |

|---|---|---|---|

| \*\*MR\*\* Modem Ready | A0 | Green | Radio initialised and healthy |

| \*\*TR\*\* Terminal Ready | A1 | Green | Serial activity in last 200 ms |

| \*\*OH\*\* Off Hook | A2 | Yellow | Any non-idle state |

| \*\*CD\*\* Carrier Detect | A3 | Yellow | Connected / data mode |

| \*\*SD\*\* Send Data | A4 | Red | Flashes 40 ms per TX packet |

| \*\*RD\*\* Receive Data | A5 | Red | Flashes 40 ms per RX packet |

| \*\*HS\*\* High Speed | D2 | Green | 2 Mbps mode active |

| \*\*ER\*\* Error | D3 | Red | TX failure / NO CARRIER / data loss |



> \*\*MR\*\* goes dark and \*\*ER\*\* blinks continuously if the nRF24L01 fails to initialise or is disconnected at runtime.



\---



\## Dependencies



Install via \*\*Sketch → Include Library → Manage Libraries\*\*:



\- \[RF24 by TMRh20](https://github.com/nRF24/RF24) — nRF24L01 driver



\---



\## Getting Started



1\. Flash the same sketch to \*\*both\*\* Arduinos.

2\. Open a serial terminal at \*\*115 200 baud\*\* (default).

3\. Set each node's MAC address:

&#x20;  ```

&#x20;  ATSMYMAC=A1B2C3    → OK

&#x20;  ATSMYMAC=D4E5F6    → OK   (on the other node)

&#x20;  ```

4\. On node B, optionally enable auto-answer:

&#x20;  ```

&#x20;  ATS0=1             → OK

&#x20;  ```

5\. From node A, dial node B:

&#x20;  ```

&#x20;  ATD D4E5F6         → CONNECT

&#x20;  ```

6\. Type freely — data flows both ways. Use `+++` (with 1 s silence either side) to return to command mode without hanging up.



\---



\## AT Command Reference



\### Connection



| Command | Description |

|---|---|

| `ATD XXYYZZ` | Dial remote MAC `XXYYZZ` (6 hex digits) |

| `ATA` | Manually answer an incoming call |

| `ATH` | Hang up or reject an incoming call |

| `ATS0=n` | Auto-answer after \*n\* rings (0 = off) |

| `+++` | Escape from data mode to command mode (1 s guard each side) |



\### Configuration



| Command | Description |

|---|---|

| `ATI` | Print full modem status (radio, channel, MACs, RSSI, S-registers, drop counters) |

| `ATE0` / `ATE1` | Echo off / on |

| `ATSMYMAC=XXYYZZ` | Set own 3-byte MAC, saved to EEPROM |

| `ATSMYMAC?` | Query own MAC |

| `ATSETCH=nn` | Set RF channel 0–125 |

| `ATSETCH?` | Query RF channel |

| `ATSSPEED=n` | Set air data rate: `0`=250 kbps  `1`=1 Mbps  `2`=2 Mbps |

| `ATSSPEED?` | Query air data rate |

| `ATSBAUD=n` | Set serial baud rate (see table below). OK sent at old rate, then switches. |

| `ATSBAUD?` | Query current baud rate |

| `ATSHWACK=n` | ACK mode: `1`=hardware ACK (default)  `0`=software flow control |

| `ATSHWACK?` | Query ACK mode |

| `AT\&F` | Factory reset — restores all defaults, overwrites EEPROM |



\### Stored Dial Strings \& Autodial



| Command | Description |

|---|---|

| `AT\&Zn=string` | Store dial string in slot \*n\* (0–3). Empty string clears slot. |

| `AT\&Zn?` | Query stored string in slot \*n\* |

| `AT\&Yn` | Set startup autodial slot (fires 2 s after boot) |

| `AT\&Y?` | Query startup slot (`none` if disabled) |



\### S-Registers



Query with `ATSn?` (returns zero-padded 3 digits). Set with `ATSn=value`. All saved to EEPROM.



| Register | Default | Range | Description |

|---|---|---|---|

| S0 | 0 | 0–255 | Auto-answer ring count (0 = disabled) |

| S6 | 0 | 0–255 s | Pre-dial wait (0 = none; no dialtone on radio) |

| S7 | 1 | 1–255 s | Carrier wait / connect timeout |

| S8 | 3 | 0–255 | Dial retry attempts after failure (0 = no retry) |

| S9 | 10 | 0–255 s | Inter-retry interval |

| S10 | 1 | 0–1 | Keep-alive enable (1 = on) |

| S11 | 5 | 1–255 s | Keep-alive ping interval |

| S12 | 3 | 1–255 | Missed pings before `NO CARRIER` |

| S13 | 1 | 0–1 | Flow control: `0`=none  `1`=XON/XOFF |



\---



\## Supported Baud Rates



| Value | Notes |

|---|---|

| 9600 | Minimum; radio is starved |

| 19200 | |

| 38400 | |

| 57600 | Well matched to HWACK mode (\~50 kbps) |

| \*\*115200\*\* | \*\*Default\*\* — best match for SWFLOW mode |

| 250000 | Exceeds radio ceiling; serial no longer bottleneck |

| 500000 | Recommended for maximum throughput |

| 1000000 | Works on CH340G; verify with your USB-serial chip |



\---



\## Link Modes



\### HWACK mode (`ATSHWACK=1`, default)



Hardware acknowledgement via the nRF24L01's built-in auto-ACK and retry mechanism. Every 32-byte packet is confirmed before the next is sent. Reliable on noisy links; throughput limited to \~50 kbps by the ACK turnaround.



\### SWFLOW mode (`ATSHWACK=0`)



Auto-ACK disabled. Packets fly continuously up to a 4-slot retransmit window. The receiver sends `PKT\_NACK` on gaps and `PKT\_SWACK` (cumulative ACK) on each in-order packet. A 150 ms retransmit timer catches lost packets when both data and NACK are dropped. Throughput \~80–100 kbps on a clean link.



\*\*Maximum throughput settings:\*\*

```

ATSSPEED=2        (2 Mbps air rate)

ATSHWACK=0        (software flow control)

ATSBAUD=500000    (serial ceiling well above radio limit)

```



\---



\## Keep-Alive



When `S10=1`, the \*\*dialling side\*\* (initiator) sends a `PKT\_PING` every S11 seconds. The \*\*answering side\*\* always replies with `PKT\_PONG` regardless of its own S10 setting.



| Side | S10=1 | S10=0 |

|---|---|---|

| \*\*Initiator\*\* | Sends pings, counts missed pongs, drops after S12 misses | No pings sent |

| \*\*Answerer\*\* | Replies to all pings AND tracks silence: drops after `S11 × S12` seconds with no ping received | Replies to all pings, never drops due to silence |



The answerer's timeout window (`S11 × S12` seconds, default 15 s) matches the time the initiator takes to exhaust all its retries, so both sides reach `NO CARRIER` at approximately the same time.



\---



\## Flow Control



XON/XOFF is active by default (`S13=1`). The sketch sends `0x13` (XOFF) when its buffer exceeds 75% capacity and `0x11` (XON) when it drains below 25%. The same mechanism is applied over the radio link.



Set `ATS13=0` if your terminal does not honour XON/XOFF. With flow control off, `0x11`/`0x13` bytes pass through as plain data. Buffer overflow is tracked and reported:



```

ATI output (after overflow):

&#x20; TX drop : 142 bytes (serial->radio, host overflow)

&#x20; RX drop : 0 bytes (radio->serial, radio overflow)

&#x20; \*\* DATA LOSS DETECTED — consider enabling XON/XOFF (ATS13=1) \*\*

```



The \*\*ER LED\*\* flashes on every dropped byte. Counters reset after each `ATI` call.



\---



\## Packet Protocol



All packets are 32 bytes (nRF24L01 fixed payload). Header is 3 bytes, leaving 29 bytes of user payload per packet.



```

Byte 0  — type

Byte 1  — sequence number (0–255, rolling)

Byte 2  — payload length (0–29)

Bytes 3–31 — payload

```



| Type | Value | Description |

|---|---|---|

| PKT\_DATA | 0x01 | User data |

| PKT\_XON | 0x02 | Resume sending (flow control) |

| PKT\_XOFF | 0x03 | Pause sending (flow control) |

| PKT\_DISC | 0x04 | Disconnect / hang up |

| PKT\_CONN | 0x05 | Connection request (carries caller MAC) |

| PKT\_ACK | 0x06 | Connection accepted |

| PKT\_NACK | 0x07 | SWFLOW: retransmit request |

| PKT\_SWACK | 0x08 | SWFLOW: cumulative acknowledgement |

| PKT\_PING | 0x09 | Keep-alive ping |

| PKT\_PONG | 0x0A | Keep-alive reply |



Pipe addresses are derived from the 3-byte MAC: `0xAB 0xCD 0xEF \[mac0] \[mac1^mac2]`, ensuring unique 5-byte nRF24L01 addresses per node.



\---



\## EEPROM Layout



89 bytes used (addresses 0–88), magic byte `0xA5` at offset 8.



| Offset | Size | Contents |

|---|---|---|

| 0–2 | 3 B | Own MAC |

| 3–5 | 3 B | Remote MAC (last dialled) |

| 6 | 1 B | RF channel |

| 7 | 1 B | Speed enum (0/1/2) |

| 8 | 1 B | Magic byte (`0xA5`) |

| 9 | 1 B | S0 |

| 10 | 1 B | HWACK mode |

| 11 | 1 B | Baud index |

| 12–79 | 68 B | Dial strings × 4 (17 bytes each) |

| 80 | 1 B | Startup slot |

| 81–88 | 8 B | S6–S13 |



If the magic byte is invalid on boot, all settings default to compile-time values and EEPROM is left untouched until the first `saveConfig()` call.



\---



\## Troubleshooting



| Symptom | Likely cause | Fix |

|---|---|---|

| \*\*MR off, ER blinking at boot\*\* | nRF24L01 not detected | Check wiring / power supply |

| \*\*ER flashing during transfer\*\* | Buffer overflow / data loss | Enable XON/XOFF (`ATS13=1`) or reduce baud |

| \*\*NO CARRIER immediately on ATD\*\* | Remote MAC not listening, wrong channel | Check `ATSETCH` matches on both nodes |

| \*\*Garbled output after ATSBAUD\*\* | Terminal not switched to new rate | Change terminal baud to match, or reboot to reload from EEPROM |

| \*\*Connection drops after \~15 s idle\*\* | Keep-alive timeout | Set `ATS10=0` to disable, or increase S11/S12 |

| \*\*Binary transfers corrupt\*\* | XON/XOFF intercepting 0x11/0x13 data bytes | Set `ATS13=0` |



\---



\## License



MIT — free to use, modify, and distribute. Attribution appreciated.




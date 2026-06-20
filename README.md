nRF24L01 Hayes-compatible AT Modem

Works on Arduino Uno / Nano

 *

Wiring (nRF24L01 module):



  VCC  → 3.3 V  (or 5 V with adapter board)

  GND  → GND

  CE   → D7  (RF-Nano onboard wiring)

  CSN  → D8  (RF-Nano onboard wiring)

  SCK  → D13

  MOSI → D11

  MISO → D12

  IRQ  → not connected (polled mode)

 *

Status LEDs (all active-HIGH, connect LED + 330 Ω resistor to GND):

  MR (Modem Ready)    → A0   Green  – radio initialised and idle

  TR (Terminal Ready) → A1   Green  – serial port active, host sending data

  OH (Off Hook)       → A2   Yellow – connection in progress or established

  CD (Carrier Detect) → A3   Yellow – remote carrier present (CONNECTED/DATA)

  SD (Send Data)      → A4   Red    – flashes on each transmitted packet

  RD (Receive Data)   → A5   Red    – flashes on each received packet

  HS (High Speed)     → D2   Green  – lights when speed = 2 Mbps

  ER (Error)          → D3   Red    – lights briefly on TX failure / NO CARRIER

 *

Nano / Uno pin map summary:

  D2  HS   D3  ER

  A0  MR   A1  TR   A2  OH   A3  CD   A4  SD   A5  RD

 *

Library required: RF24 by TMRh20

  Install via Sketch → Include Library → Manage Libraries

 *

AT commands:

  ATI              – print modem info (model, channel, own MAC, remote MAC, RSSI)

  ATSMYMAC=XXYYZZ  – set own 3-byte MAC (hex, e.g. ATSMYMAC=A1B2C3)

  ATSMYMAC?        – query own MAC

  ATSETCH=nn       – set RF channel 0-125 (e.g. ATSETCH=76)

  ATSETCH?         – query RF channel

  ATSSPEED=n       – set link speed: 0=250k, 1=1M, 2=2M

  ATSSPEED?        – query link speed

  ATD XXYYZZ       – dial / connect to remote MAC

  ATA              – manually answer an incoming call

  ATH              – hang up / reject incoming call

  ATSn=value       – set S-register n to value (all saved to EEPROM):

                     S0  auto-answer rings (0=off)

                     S6  pre-dial wait seconds (default 0, radio needs no dialtone)

                     S7  carrier wait / connect timeout seconds (default 1, radio ACK is near-instant)

                     S8  dial retry attempts after failure (default 3, 0=no retry)

                     S9  inter-retry interval seconds (default 10)

                     S10 keep-alive enable (1=on default, 0=off)

                     S11 keep-alive interval seconds (default 5)

                     S12 missed pongs before drop (default 3)

                     S13 flow control: 0=none, 1=XON/XOFF (default 1)

  ATSn?            – query S-register n (returns zero-padded 3-digit value)

  AT&F            – factory reset: restore all defaults and overwrite EEPROM

  AT&Zn=string     – store dial string in slot n (n=0-3); AT&Zn= clears slot

  AT&Zn?           – query stored dial string in slot n

  AT&Yn            – set startup autodial slot (fires 2 s after boot)

  AT&Y?            – query startup autodial slot

  ATSBAUD=n        – set baud rate (9600/19200/38400/57600/115200/250000/500000/1000000)

                   OK is sent at old rate, then port switches — match your terminal!

  ATSBAUD?         – query current baud rate

  ATSHWACK=n       – set ACK mode: 1=hardware ACK (default), 0=software flow control

  ATSHWACK?        – query ACK mode

  ATE0 / ATE1      – echo off / on

  +++              – escape data mode → command mode (1 s guard time each side)

 *

XON/XOFF flow control (active when S13=1, default ON):

  DC1 (0x11) = XON  – resume sending

  DC3 (0x13) = XOFF – stop sending

  Applied both on the serial port (to/from host) and over the air.

  Set ATS13=0 to disable if host terminal does not support XON/XOFF.

  With S13=0: 0x11/0x13 bytes pass through as data; no flow signals sent.

 *

Packet format (32 bytes max nRF24 payload):

  [0]  type   – PKT_DATA=0x01, PKT_XON=0x02, PKT_XOFF=0x03,

                PKT_DISC=0x04, PKT_CONN=0x05, PKT_ACK=0x06,

                PKT_NACK=0x07 (SWFLOW retransmit request),

                PKT_SWACK=0x08 (SWFLOW cumulative ACK),

                PKT_PING=0x09 (keep-alive ping),

                PKT_PONG=0x0A (keep-alive reply)

  [1]  seq    – rolling 0-255 sequence number

  [2]  length – number of payload bytes that follow (0-29)

  [3..31] payload


/*
 * nRF24L01 Hayes-compatible AT Modem
 * Works on Arduino Uno / Nano
 *
 * Wiring (nRF24L01 module):
 *   VCC  → 3.3 V  (or 5 V with adapter board)
 *   GND  → GND
 *   CE   → D7  (RF-Nano onboard wiring)
 *   CSN  → D8  (RF-Nano onboard wiring)
 *   SCK  → D13
 *   MOSI → D11
 *   MISO → D12
 *   IRQ  → not connected (polled mode)
 *
 * Status LEDs (all active-HIGH, connect LED + 330 Ω resistor to GND):
 *   MR (Modem Ready)    → A0   Green  – radio initialised and idle
 *   TR (Terminal Ready) → A1   Green  – serial port active, host sending data
 *   OH (Off Hook)       → A2   Yellow – connection in progress or established
 *   CD (Carrier Detect) → A3   Yellow – remote carrier present (CONNECTED/DATA)
 *   SD (Send Data)      → A4   Red    – flashes on each transmitted packet
 *   RD (Receive Data)   → A5   Red    – flashes on each received packet
 *   HS (High Speed)     → D2   Green  – lights when speed = 2 Mbps
 *   ER (Error)          → D3   Red    – lights briefly on TX failure / NO CARRIER
 *
 * Nano / Uno pin map summary:
 *   D2  HS   D3  ER
 *   A0  MR   A1  TR   A2  OH   A3  CD   A4  SD   A5  RD
 *
 * Library required: RF24 by TMRh20
 *   Install via Sketch → Include Library → Manage Libraries
 *
 * AT commands (connection):
 *   ATD XXYYZZ       – dial remote MAC; BUSY if channel busy (S14=1)
 *   ATA              – manually answer incoming call
 *   ATO              – return to data mode after +++ escape
 *   ATH              – hang up active connection (NO CARRIER) or reject ring (OK)
 *   ATS0=n           – auto-answer after n rings (0=off)
 *   +++              – escape data mode to command mode (1 s guard each side)
 *
 * AT commands (configuration):
 *   ATI              – print full modem status (radio, state, MACs, RSSI, S-regs, counters)
 *   ATE0 / ATE1      – echo off / on
 *   ATSMYMAC=XXYYZZ  – set own 3-byte MAC (hex); saved to EEPROM
 *   ATSMYMAC?        – query own MAC
 *   ATSETCH=nn       – set RF channel 0-125 (default 97 = 2497 MHz, above WiFi+BT)
 *   ATSETCH?         – query RF channel
 *   ATSSPEED=n       – air data rate: 0=250 kbps, 1=1 Mbps (default), 2=2 Mbps
 *   ATSSPEED?        – query air data rate
 *   ATSBAUD=n        – set serial baud rate (2400/4800/9600/19200/38400/57600/
 *                      115200/250000/500000/1000000); OK at old rate then switches
 *   ATSBAUD?         – query current baud rate
 *   ATSFLOW=n        – link mode: 0=transparent, 1=HWACK, 2=SWFLOW (default)
 *   ATSFLOW?         – query link mode
 *   AT&F             – factory reset: restore all defaults, rewrite EEPROM
 *   AT&Zn=string     – store dial string in slot n (0-3); AT&Zn= clears slot
 *   AT&Zn?           – query stored dial string in slot n
 *   AT&Yn            – set startup autodial slot (fires 2 s after boot)
 *   AT&Y?            – query startup autodial slot
 *
 * AT commands (diagnostics):
 *   ATSPECTRUM       – sweep 126 channels, print ASCII spectrum; any key stops
 *   ATPINGXXYYZZ     – diagnostic ping to MAC XXYYZZ (idle only); prints PONG from XXXXXX
 *   ATREBOOT         – hardware reboot via watchdog (boots fresh from EEPROM)
 *   ATTEST-TX        – speed test TX: flood sequenced packets; paired with ATTEST-RX
 *   ATTEST-RX        – speed test RX: arm on magic, report throughput + link quality
 *   ATTEST-ECHO      – speed test echo: reflect received packets back to sender
 *
 * S-registers (ATSn=value to set, ATSn? to query; all saved to EEPROM):
 *   S0   auto-answer ring count (0=off, default 0)
 *   S6   pre-dial wait seconds (0=none, default 0, non-blocking)
 *   S7   carrier wait / connect timeout per attempt, seconds (default 3)
 *        also used as ATPING reply timeout
 *   S8   dial retry attempts after failure (0=no retry, 255=forever, default 3)
 *   S9   inter-retry interval seconds (default 3)
 *   S10  keep-alive enable: 1=on (default), 0=off
 *   S11  keep-alive ping interval seconds (default 5)
 *   S12  missed pings before NO CARRIER (default 3)
 *   S13  XON/XOFF flow control: 1=on (default), 0=off
 *        applies to both serial and radio links
 *   S14  busy detect before dial: 1=on (default), 0=off
 *   S15  busy detect sample count (each ~2 ms, default 50 ≈ 100 ms scan)
 *   S16  transparent mode TX idle flush timeout ms (default 5)
 *   S17  spectrum scan samples per channel (each ~500 µs, default 20)
 *   S18  silent mode: 1=suppress all CLI serial output, 0=normal (default 0)
 *        LEDs are unaffected. ATS18=0 always works even when silent.
 *
 * XON/XOFF flow control (active when S13=1, default ON):
 *   DC1 (0x11) = XON  – resume sending
 *   DC3 (0x13) = XOFF – stop sending
 *   Applied both on the serial port (to/from host) and over the air.
 *   Set ATS13=0 to disable if host terminal does not support XON/XOFF.
 *   With S13=0: 0x11/0x13 bytes pass through as data; no flow signals sent.
 *
 * Packet format (32 bytes max nRF24 payload):
 *   [0]  type   – PKT_DATA=0x01, PKT_XON=0x02, PKT_XOFF=0x03,
 *                 PKT_DISC=0x04, PKT_CONN=0x05, PKT_ACK=0x06,
 *                 PKT_NACK=0x07 (SWFLOW retransmit request),
 *                 PKT_SWACK=0x08 (SWFLOW cumulative ACK),
 *                 PKT_PING=0x09 (keep-alive ping),
 *                 PKT_PONG=0x0A (keep-alive reply),
 *                 PKT_SWACK_YIELD=0x0B (SWFLOW ACK + yield TX token to remote)
 *                 PKT_DIAG_PING=0x0C  (diagnostic ping, ATPING command)
 *                 PKT_DIAG_PONG=0x0D  (diagnostic pong, reply to ATPING)
 *                 PKT_TEST_START=0x0E (speed test start / re-arm)
 *                 PKT_TEST_STOP=0x0F  (speed test stop from either side)
 *   [1]  seq    – rolling 0-255 sequence number
 *   [2]  length – number of payload bytes that follow (0-29)
 *   [3..31] payload
 */

#include <SPI.h>
#include <RF24.h>
#include <avr/wdt.h>    // watchdog — used by ATREBOOT
#include <EEPROM.h>

// ── Firmware version ───────────────────────────────────────────────────────────
// Increment minor version (v1.x.0) on every code modification.
#define MODEM_VERSION "v1.61.0"

// ── Pin config ────────────────────────────────────────────────────────────────
#define CE_PIN   7    // RF-Nano: nRF24L01 CE  hardwired to D7
#define CSN_PIN  8    // RF-Nano: nRF24L01 CSN hardwired to D8

// Status LEDs — connect each via 330 Ω resistor in series to GND, anode to pin.
#define LED_MR   A0   // Modem Ready    (green)  – radio OK and idle
#define LED_TR   A1   // Terminal Ready (green)  – serial active / host typing
#define LED_OH   A2   // Off Hook       (yellow) – not idle (dialling/connected)
#define LED_CD   A3   // Carrier Detect (yellow) – remote carrier present
#define LED_SD   A4   // Send Data      (red)    – flashes each TX packet
#define LED_RD   A5   // Receive Data   (red)    – flashes each RX packet
#define LED_HS   2    // High Speed     (green)  – 2 Mbps mode active
#define LED_ER   3    // Error          (red)    – TX fail / NO CARRIER

#define LED_FLASH_MS   40    // SD / RD pulse width (ms)
#define LED_ER_MS     400    // ER pulse width (ms)

// ── Buffer sizes ──────────────────────────────────────────────────────────────
#define RX_BUF_SIZE   256   // RX circular buffer (radio→serial). 982 B headroom allows 256.
#define TX_BUF_SIZE   256   // TX circular buffer (serial→radio).
#define CMD_BUF_SIZE   64

// XON/XOFF thresholds — recalculated for 256-byte buffers
#define XOFF_THRESHOLD 192   // 75 % of RX_BUF_SIZE → send XOFF (~6.6 packets)
#define XON_THRESHOLD   64   // 25 % of RX_BUF_SIZE → send XON

// ── Packet types ──────────────────────────────────────────────────────────────
#define PKT_DATA  0x01
#define PKT_XON   0x02
#define PKT_XOFF  0x03
#define PKT_DISC  0x04
#define PKT_CONN  0x05
#define PKT_ACK   0x06
#define PKT_NACK  0x07   // SWFLOW: request retransmit of seq N
#define PKT_SWACK 0x08   // SWFLOW: cumulative ACK up to seq N
#define PKT_PING        0x09   // keep-alive ping  (initiator -> remote)
#define PKT_PONG        0x0A   // keep-alive reply (remote   -> initiator)
#define PKT_SWACK_YIELD 0x0B   // SWFLOW: cumulative ACK + yield TX to remote
#define PKT_DIAG_PING   0x0C   // diagnostic ping  (ATPING command, idle-only)
#define PKT_DIAG_PONG   0x0D   // diagnostic pong  (reply to PKT_DIAG_PING)
#define PKT_TEST_START  0x0E   // speed test start / re-arm signal
#define PKT_TEST_STOP   0x0F   // speed test stop  (either side to other)

#define PAYLOAD_SIZE  32
#define DATA_OFFSET    3
#define MAX_DATA      (PAYLOAD_SIZE - DATA_OFFSET)   // 29

// ── EEPROM layout (each field 3 bytes) ───────────────────────────────────────
#define EE_OWN_MAC    0    // bytes 0-2  own MAC
#define EE_REM_MAC    3    // bytes 3-5  remote MAC (last dialled)
#define EE_CHANNEL    6    // byte  6    RF channel
#define EE_SPEED      7    // byte  7    data rate enum
#define EE_MAGIC      8    // byte  8    0xA5 if EEPROM valid
#define EE_S0         9    // byte  9    S0 auto-answer ring count
#define EE_FLOW      10    // byte 10    flow/ACK mode: 0=none, 1=HWACK, 2=SWFLOW
#define EE_BAUD      11    // byte 11    baud rate index (see baudTable)
#define EE_DIALSTR0  12    // bytes 12-28  dial string slot 0 (17 bytes incl NUL)
#define EE_DIALSTR1  29    // bytes 29-45  dial string slot 1
#define EE_DIALSTR2  46    // bytes 46-62  dial string slot 2
#define EE_DIALSTR3  63    // bytes 63-79  dial string slot 3
#define EE_STARTUP   80    // byte 80      startup autodial slot (0-3, 0xFF=none)
#define EE_S6        81    // byte 81      S6: pre-dial wait (seconds)
#define EE_S7        82    // byte 82      S7: carrier wait / connect timeout (seconds)
#define EE_S8        83    // byte 83      S8: dial retry attempts (0=no retry)
#define EE_S9        84    // byte 84      S9: inter-retry interval (seconds)
#define EE_S10       85    // byte 85      S10: keep-alive enable (1=on, 0=off)
#define EE_S11       86    // byte 86      S11: keep-alive interval (seconds)
#define EE_S12       87    // byte 87      S12: missed keep-alives before drop
#define EE_S13       88    // byte 88      S13: flow control mode (0=none, 1=XON/XOFF)
#define EE_S14       89    // byte 89      S14: busy detect enable (1=on, 0=off)
#define EE_S15       90    // byte 90      S15: channel scan duration ms
#define EE_S16       91    // byte 91      S16: transparent mode TX idle flush ms
#define EE_S17       92    // byte 92      S17: spectrum scan dwell ms per channel
#define EE_S18       93    // byte 93      S18: silent mode (1=suppress all CLI output)

#define DIAL_SLOTS      4
#define DIAL_STR_LEN   16   // max chars, not counting NUL

#define EEPROM_MAGIC  0xA5

// ── Modem states ─────────────────────────────────────────────────────────────
enum ModemState { S_IDLE, S_RINGING, S_CONNECTING, S_CONNECTED, S_DATA };

// ── Globals ───────────────────────────────────────────────────────────────────
RF24 radio(CE_PIN, CSN_PIN);

ModemState state = S_IDLE;

uint8_t ownMac[3]    = {0x01, 0x02, 0x03};
uint8_t remoteMac[3] = {0x00, 0x00, 0x00};
uint8_t channel      = 97;   // 2497 MHz — above WiFi (max ch14=2484) and Bluetooth (max 2480)
uint8_t speedEnum    = 1;   // 0=250k 1=1M 2=2M
bool    echoOn       = true;
uint8_t regS0        = 0;     // S0 register: 0=manual answer, 1+=auto-answer after n rings
uint8_t regS6        = 0;     // S6: seconds to wait before first dial attempt (0=none, radio needs no dialtone)
uint8_t regS7        = 3;     // S7: seconds to wait for carrier (default 3)
uint8_t regS8        = 3;     // S8: number of retry attempts after first failure (default 3)
uint8_t regS9        = 3;     // S9: seconds between retry attempts (default 3)
uint8_t regS10       = 1;     // S10: keep-alive enable (1=on, 0=off)
uint8_t regS11       = 5;     // S11: keep-alive interval (seconds)
uint8_t regS12       = 3;     // S12: missed pongs before dropping connection
uint8_t regS13       = 1;     // S13: flow control 0=none, 1=XON/XOFF (default ON)
uint8_t regS14       = 1;     // S14: busy detect enable (1=on, 0=off)
uint8_t regS15       = 50;    // S15: channel scan duration in ms (default 50)
uint8_t regS16       = 5;     // S16: transparent mode TX idle flush ms (default 5)
uint8_t regS17       = 20;    // S17: spectrum scan dwell ms per channel (default 20)
uint8_t regS18       = 0;     // S18: silent mode 0=normal, 1=suppress all CLI output

unsigned long lastDisconnectMs = 0;  // millis() of last disconnect — busy detect grace period

// Data-loss counters (reset by ATI or AT&F)
uint32_t txDropped   = 0;    // bytes dropped: serial→radio direction (host sent too fast)
uint32_t rxDropped   = 0;    // bytes dropped: radio→serial direction (radio came in too fast)

// Keep-alive runtime state
bool          kaInitiator   = false; // true = we dialled (send pings); false = we answered (reply only)
unsigned long kaPingAt      = 0;     // millis() when next PKT_PING is due
uint8_t       kaMissed      = 0;     // consecutive unanswered pings (initiator only)
bool          kaWaitingPong = false; // true between sending PING and receiving PONG
unsigned long kaLastPingMs  = 0;     // answerer: millis() of last received PKT_PING (0=none yet)

// Dial retry state (managed by the connect-timeout block in loop())
uint32_t dialRetryCount  = 0;          // retries fired so far (uint32 for forever mode)
bool     dialRetrying    = false;      // true while waiting between retries
unsigned long dialRetryAt = 0;         // millis() when next retry fires
char     lastDialStr[DIAL_STR_LEN + 5]; // "ATD XXYYZZ" copy for retransmission

// Incoming call state (used while S_RINGING)
uint8_t  pendingMac[3]  = {0, 0, 0};  // MAC of the caller waiting to be answered
uint8_t  ringCount   = 0;   // how many RINGs sent (for S0 auto-answer threshold)
unsigned long lastConnMs = 0;  // millis() of last PKT_CONN received (ring timeout)

// Circular buffers
uint8_t rxBuf[RX_BUF_SIZE];
uint16_t rxHead = 0, rxTail = 0;

uint8_t txBuf[TX_BUF_SIZE];
uint16_t txHead = 0, txTail = 0;

// Command line accumulator
char cmdBuf[CMD_BUF_SIZE];
uint8_t cmdLen = 0;

// XON/XOFF state
bool hostXoffSent  = false;   // we told the host to stop
bool radioXoffRecv  = false;  // remote told us to stop
bool radioXoffSent  = false;  // we told remote to stop
bool    yieldToRemote  = false;  // remote sent SWACK_YIELD — pause our TX, let them send

// Pending packet: flushTxBuffer's RX wait stores here to avoid re-entrant
// handleRadioPacket() calls. Main loop drains it before radio.available().
bool    pendingPktReady = false;
uint8_t pendingPkt[PAYLOAD_SIZE];

// ── LED state ────────────────────────────────────────────────────────────────
// SD and RD are monostable: they light for LED_FLASH_MS then go dark.
// ER lights for LED_ER_MS on errors.
// MR, TR, OH, CD, HS are steady-state and updated by updateSteadyLEDs().
unsigned long ledSdOff  = 0;   // millis() time to extinguish SD
unsigned long ledRdOff  = 0;   // millis() time to extinguish RD
unsigned long ledErOff  = 0;   // millis() time to extinguish ER

// TR blinks while there is pending TX data; track last serial activity
unsigned long lastSerialMs = 0;

// Sequence numbers
uint8_t txSeq     = 0;    // sequence counter for ALL outbound packets

// Flow / ACK mode: 0=none (reserved), 1=HWACK (hardware ACK), 2=SWFLOW (software flow control, default)
uint8_t flowMode = 2;

// ── Transparent mode (flowMode == 0) TX buffer ───────────────────────────────
// Bytes accumulate here until the buffer is full (32 bytes) or the idle timer
// fires (~5 ms after last byte), then the whole buffer is sent as a raw payload.
uint8_t  transBuf[PAYLOAD_SIZE];
uint8_t  transBufLen    = 0;
unsigned long transLastByteMs = 0;   // millis() of last byte pushed to transBuf
// TRANS_IDLE_MS replaced by S16 register (regS16) — see ATSn handler

// Baud rate table — index stored in EEPROM, not the raw value (saves 2 bytes).
// Valid indices 0-9 matching baudTable[]. Default index 6 = 115200.
const uint32_t baudTable[] = {
    2400UL, 4800UL, 9600UL, 19200UL, 38400UL, 57600UL,
    115200UL, 250000UL, 500000UL, 1000000UL
};
const uint8_t  BAUD_TABLE_SIZE = 10;
const uint8_t  BAUD_DEFAULT    = 6;   // 115200
uint8_t        baudIdx         = BAUD_DEFAULT;
// Dial string profiles and startup autodial
char    dialStr[DIAL_SLOTS][DIAL_STR_LEN + 1];  // "" = empty slot
uint8_t startupSlot = 0xFF;                      // 0xFF = autodial disabled
bool    autoDial    = false;   // armed in setup() if startup slot is valid + non-empty
unsigned long autoDialMs = 0;  // millis() timestamp to fire autodial

// ── SWFLOW simple stop-and-wait ───────────────────────────────────────────────
// Sends one PKT_DATA, waits SW_ACK_WAIT_MS for a SWACK, retries up to
// SW_RETX_MAX times. No window, no NACK, no gap detection.
// Yield token embedded in SWACK — receiver sends PKT_SWACK_YIELD when it has
// data queued, giving itself a transmission window.
#define SW_ACK_WAIT_MS   5   // ms to wait for SWACK after sending each packet
#define SW_RETX_MAX      4   // max retransmit attempts before dropping packet

uint8_t swLastPkt[PAYLOAD_SIZE];  // last sent PKT_DATA for retransmit
bool    swLastPktValid = false;   // true when swLastPkt holds a valid packet
uint32_t swRetxCount   = 0;       // total retransmit attempts this session

// Radio health
bool     radioFailed   = false;  // set on init fail or runtime disconnect
unsigned long lastHealthMs = 0;  // last time we ran isChipConnected()
#define HEALTH_INTERVAL_MS  500  // check every 500 ms

// Escape sequence (+++) detection
unsigned long lastDataMs = 0;    // millis() of last byte in data mode
uint8_t  plusCount   = 0;        // count of consecutive '+' chars received
bool     escapeArmed = false;    // true after first guard silence

// SWFLOW duplicate detection
uint8_t  rxLastSeq   = 0xFF;    // last accepted PKT_DATA seq (0xFF = none yet)
bool     testStopFlag = false;  // set by flushTxBuffer when PKT_TEST_STOP received

// ── LED helpers ───────────────────────────────────────────────────────────────

// Call once in setup() to initialise all LED pins.
void ledSetup() {
    const uint8_t pins[] = {LED_MR, LED_TR, LED_OH, LED_CD,
                             LED_SD, LED_RD, LED_HS, LED_ER};
    for (uint8_t i = 0; i < sizeof(pins); i++) {
        pinMode(pins[i], OUTPUT);
        digitalWrite(pins[i], LOW);
    }
}

// Trigger the SD (Send Data) flash.
void ledFlashSD() {
    digitalWrite(LED_SD, HIGH);
    ledSdOff = millis() + LED_FLASH_MS;
}

// Trigger the RD (Receive Data) flash.
void ledFlashRD() {
    digitalWrite(LED_RD, HIGH);
    ledRdOff = millis() + LED_FLASH_MS;
}

// Trigger the ER (Error) flash — longer pulse so it's visible.
void ledFlashER() {
    digitalWrite(LED_ER, HIGH);
    ledErOff = millis() + LED_ER_MS;
}

// Update all steady-state LEDs to reflect current modem state.
// Call every loop iteration — it's cheap (just digitalWrite).
void updateSteadyLEDs() {
    // MR – Modem Ready: on while radio is healthy; off on fault.
    digitalWrite(LED_MR, radioFailed ? LOW : HIGH);

    // TR – Terminal Ready: on while serial port is actively receiving characters.
    // We keep it lit for 200 ms after the last received serial byte.
    digitalWrite(LED_TR, (millis() - lastSerialMs < 200) ? HIGH : LOW);

    // OH – Off Hook: any non-idle state including ringing
    digitalWrite(LED_OH, (state != S_IDLE) ? HIGH : LOW);

    // CD – Carrier Detect: connected or in data mode
    digitalWrite(LED_CD, (state == S_CONNECTED || state == S_DATA) ? HIGH : LOW);

    // HS – High Speed: 2 Mbps mode
    digitalWrite(LED_HS, (speedEnum == 2) ? HIGH : LOW);
}

// Expire monostable LEDs whose timer has elapsed.
void updateFlashLEDs() {
    unsigned long now = millis();
    if (ledSdOff && now >= ledSdOff) { digitalWrite(LED_SD, LOW); ledSdOff = 0; }
    if (ledRdOff && now >= ledRdOff) { digitalWrite(LED_RD, LOW); ledRdOff = 0; }
    if (ledErOff && now >= ledErOff) {
        ledErOff = 0;
        // Keep ER lit steady if radio has failed permanently.
        if (!radioFailed) digitalWrite(LED_ER, LOW);
    }
}

// ── Pipe address helpers ──────────────────────────────────────────────────────
// 5-byte address: 0xAB 0xCD 0xEF 0x00 [mac0 mac1 mac2]
// Ensures unique addresses per MAC without needing a server.

void macToAddr(const uint8_t mac[3], uint8_t addr[5]) {
    addr[0] = 0xAB;
    addr[1] = 0xCD;
    addr[2] = mac[0];   // all 3 MAC bytes used directly — no XOR fold
    addr[3] = mac[1];   // avoids collision between MACs sharing last 2 bytes
    addr[4] = mac[2];
}

// ── EEPROM helpers ────────────────────────────────────────────────────────────
void loadConfig() {
    if (EEPROM.read(EE_MAGIC) != EEPROM_MAGIC) return;
    ownMac[0] = EEPROM.read(EE_OWN_MAC);
    ownMac[1] = EEPROM.read(EE_OWN_MAC + 1);
    ownMac[2] = EEPROM.read(EE_OWN_MAC + 2);
    remoteMac[0] = EEPROM.read(EE_REM_MAC);
    remoteMac[1] = EEPROM.read(EE_REM_MAC + 1);
    remoteMac[2] = EEPROM.read(EE_REM_MAC + 2);
    channel   = EEPROM.read(EE_CHANNEL);
    speedEnum = EEPROM.read(EE_SPEED);
    regS0     = EEPROM.read(EE_S0);
    { uint8_t v = EEPROM.read(EE_FLOW); flowMode = (v <= 2) ? v : 2; }
    uint8_t bi = EEPROM.read(EE_BAUD);
    baudIdx   = (bi < BAUD_TABLE_SIZE) ? bi : BAUD_DEFAULT;

    for (uint8_t s = 0; s < DIAL_SLOTS; s++) {
        uint8_t base = EE_DIALSTR0 + s * (DIAL_STR_LEN + 1);
        for (uint8_t c = 0; c <= DIAL_STR_LEN; c++)
            dialStr[s][c] = (char)EEPROM.read(base + c);
        dialStr[s][DIAL_STR_LEN] = '\0';
    }
    startupSlot = EEPROM.read(EE_STARTUP);
    regS6 = EEPROM.read(EE_S6); if (regS6 == 0xFF) regS6 = 0;
    regS7 = EEPROM.read(EE_S7); if (regS7 == 0xFF) regS7 = 3;
    regS8 = EEPROM.read(EE_S8);
    // 0xFF raw = 255 = forever; only default to 3 if EEPROM magic invalid
    if (regS8 == 0xFF && EEPROM.read(EE_MAGIC) != EEPROM_MAGIC) regS8 = 3;
    regS9  = EEPROM.read(EE_S9);  if (regS9  == 0xFF) regS9  = 3;
    regS10 = EEPROM.read(EE_S10); if (regS10 == 0xFF) regS10 = 1;
    regS11 = EEPROM.read(EE_S11); if (regS11 == 0xFF) regS11 = 5;
    regS12 = EEPROM.read(EE_S12); if (regS12 == 0xFF) regS12 = 3;
    regS13 = EEPROM.read(EE_S13); if (regS13 > 1)       regS13 = 1;
    regS14 = EEPROM.read(EE_S14); if (regS14 > 1)       regS14 = 1;
    regS15 = EEPROM.read(EE_S15); if (regS15 == 0 || regS15 == 0xFF) regS15 = 50;
    regS16 = EEPROM.read(EE_S16); if (regS16 == 0 || regS16 == 0xFF) regS16 = 5;
    regS17 = EEPROM.read(EE_S17); if (regS17 == 0 || regS17 == 0xFF) regS17 = 20;
    regS18 = EEPROM.read(EE_S18); if (regS18 > 1)                    regS18 = 0;
}

void saveConfig() {
    // EEPROM.update() reads before writing — skips the write if value
    // is unchanged, dramatically reducing wear on frequently-saved bytes.
    EEPROM.update(EE_OWN_MAC,     ownMac[0]);
    EEPROM.update(EE_OWN_MAC + 1, ownMac[1]);
    EEPROM.update(EE_OWN_MAC + 2, ownMac[2]);
    EEPROM.update(EE_REM_MAC,     remoteMac[0]);
    EEPROM.update(EE_REM_MAC + 1, remoteMac[1]);
    EEPROM.update(EE_REM_MAC + 2, remoteMac[2]);
    EEPROM.update(EE_CHANNEL,  channel);
    EEPROM.update(EE_SPEED,    speedEnum);
    EEPROM.update(EE_S0,       regS0);
    EEPROM.update(EE_FLOW,     flowMode);
    EEPROM.update(EE_BAUD,     baudIdx);
    for (uint8_t s = 0; s < DIAL_SLOTS; s++) {
        uint8_t base = EE_DIALSTR0 + s * (DIAL_STR_LEN + 1);
        for (uint8_t c = 0; c <= DIAL_STR_LEN; c++)
            EEPROM.update(base + c, (uint8_t)dialStr[s][c]);
    }
    EEPROM.update(EE_STARTUP,  startupSlot);
    EEPROM.update(EE_S6,       regS6);
    EEPROM.update(EE_S7,       regS7);
    EEPROM.update(EE_S8,       regS8);
    EEPROM.update(EE_S9,       regS9);
    EEPROM.update(EE_S10,      regS10);
    EEPROM.update(EE_S11,      regS11);
    EEPROM.update(EE_S12,      regS12);
    EEPROM.update(EE_S13,      regS13);
    EEPROM.update(EE_S14,      regS14);
    EEPROM.update(EE_S15,      regS15);
    EEPROM.update(EE_S16,      regS16);
    EEPROM.update(EE_S17,      regS17);
    EEPROM.update(EE_S18,      regS18);
    EEPROM.update(EE_MAGIC,    EEPROM_MAGIC);
}

// ── Radio setup ───────────────────────────────────────────────────────────────
rf24_datarate_e speedToRate(uint8_t s) {
    if (s == 0) return RF24_250KBPS;
    if (s == 2) return RF24_2MBPS;
    return RF24_1MBPS;
}

void applyRadioConfig() {
    radio.setChannel(channel);
    radio.setDataRate(speedToRate(speedEnum));
    radio.setPayloadSize(PAYLOAD_SIZE);
    radio.setPALevel(RF24_PA_MAX);
    if (flowMode == 1) {   // HWACK
        radio.setAutoAck(true);
        radio.setRetries(5, 15);   // 5 × 250 µs delay, up to 15 retries
    } else {                   // SWFLOW (2) or none (0) — no hardware ACK
        radio.setAutoAck(false);
        radio.setRetries(0, 0);
    }
}

void openListenPipes() {
    uint8_t addr[5];
    macToAddr(ownMac, addr);
    radio.openReadingPipe(1, addr);
    radio.startListening();
}

void openWritePipe(const uint8_t mac[3]) {
    uint8_t addr[5];
    macToAddr(mac, addr);
    radio.stopListening();
    radio.openWritingPipe(addr);
}

// ── Circular buffer helpers ───────────────────────────────────────────────────
inline uint16_t rxAvail() {
    return (rxHead - rxTail + RX_BUF_SIZE) % RX_BUF_SIZE;
}
inline bool rxPush(uint8_t b) {
    uint16_t next = (rxHead + 1) % RX_BUF_SIZE;
    if (next == rxTail) { rxDropped++; ledFlashER(); return false; }  // full — count + blink
    rxBuf[rxHead] = b;
    rxHead = next;
    return true;
}
inline int rxPop() {
    if (rxHead == rxTail) return -1;
    uint8_t b = rxBuf[rxTail];
    rxTail = (rxTail + 1) % RX_BUF_SIZE;
    return b;
}

inline uint16_t txAvail() {
    return (txHead - txTail + TX_BUF_SIZE) % TX_BUF_SIZE;
}
inline bool txPush(uint8_t b) {
    uint16_t next = (txHead + 1) % TX_BUF_SIZE;
    if (next == txTail) { txDropped++; ledFlashER(); return false; }  // full — count + blink
    txBuf[txHead] = b;
    txHead = next;
    return true;
}
inline int txPop() {
    if (txHead == txTail) return -1;
    uint8_t b = txBuf[txTail];
    txTail = (txTail + 1) % TX_BUF_SIZE;   // wrap BEFORE return — was dead code!
    return b;
}

// ── Buffer flush ─────────────────────────────────────────────────────────────
// Call on every disconnect and reconnect to prevent stale data from a previous
// session leaking into the new one.
void clearBuffers() {
    rxHead = rxTail = 0;
    txHead = txTail = 0;
}

// ── RSSI approximation ────────────────────────────────────────────────────────
// nRF24L01 has no RSSI register; we use carrier detect (CD) on the
// receive channel to indicate signal presence. For a rough dBm estimate
// we scan the current channel 64 times and count CD hits.
int8_t readRSSI() {
    radio.startListening();
    delayMicroseconds(128);
    uint8_t hits = 0;
    for (uint8_t i = 0; i < 64; i++) {
        if (radio.testCarrier()) hits++;
        delayMicroseconds(8);
    }
    // Map 0-64 hits → approximately -90 to -30 dBm
    int8_t rssi = -90 + (int8_t)((hits * 60) / 64);
    return rssi;
}

// ── Response helpers ──────────────────────────────────────────────────────────
void sendOK()        { if (!regS18) Serial.print(F("\r\nOK\r\n")); }
void sendError()     { if (!regS18) Serial.print(F("\r\nERROR\r\n")); }
void sendNoCarrier() { if (!regS18) Serial.print(F("\r\nNO CARRIER\r\n")); ledFlashER(); }
void sendConnect()   { if (!regS18) Serial.print(F("\r\nCONNECT\r\n")); }
void sendRing()      { if (!regS18) Serial.print(F("\r\nRING\r\n")); }

// Print diagnostic/unsolicited text only when in CLI mode (not raw data mode).
// In S_DATA the serial stream is raw — injecting text corrupts it.
// S_CONNECTED = data mode but user has escaped via +++ so CLI is restored.
inline bool inCliMode() { return state != S_DATA; }
void cliPrint(const __FlashStringHelper *s)   { if (inCliMode() && !regS18) Serial.print(s); }
void cliPrintln(const __FlashStringHelper *s) { if (inCliMode() && !regS18) Serial.println(s); }
void cliPrint(uint8_t v)                      { if (inCliMode() && !regS18) Serial.print(v); }
void cliPrintln(uint8_t v)                    { if (inCliMode() && !regS18) Serial.println(v); }
void cliPrint(char c)                         { if (inCliMode() && !regS18) Serial.print(c); }
void cliPrint(uint32_t v)                     { if (inCliMode() && !regS18) Serial.print(v); }


// ── Packet transmit ───────────────────────────────────────────────────────────
bool sendPacket(uint8_t type, const uint8_t *data, uint8_t len) {
    uint8_t pkt[PAYLOAD_SIZE];
    memset(pkt, 0, PAYLOAD_SIZE);
    pkt[0] = type;
    pkt[1] = txSeq++;
    pkt[2] = (len > MAX_DATA) ? MAX_DATA : len;
    if (data && pkt[2] > 0)
        memcpy(pkt + DATA_OFFSET, data, pkt[2]);

    // In SWFLOW mode, store PKT_DATA for possible retransmit.
    if (flowMode == 2 && type == PKT_DATA) {
        memcpy(swLastPkt, pkt, PAYLOAD_SIZE);
        swLastPktValid = true;
    }

    openWritePipe(remoteMac);
    bool ok = radio.write(pkt, PAYLOAD_SIZE);
    if (ok) ledFlashSD(); else ledFlashER();
    openListenPipes();
    return ok;
}

bool sendControlPacket(uint8_t type) {
    return sendPacket(type, nullptr, 0);
}

// ── Send buffered TX data over the air ───────────────────────────────────────
// SWFLOW: pure stop-and-wait. Send one packet, wait SW_ACK_WAIT_MS for SWACK.
// Retransmit up to SW_RETX_MAX times on no-reply.
// Yield: PKT_SWACK_YIELD from receiver → wait for their packet, then return.
void flushTxBuffer() {
    if (state != S_DATA && state != S_CONNECTED) return;
    if (radioXoffRecv) return;

    // Cooperative yield: remote requested TX window.
    if (yieldToRemote) {
        yieldToRemote = false;
        unsigned long yieldEnd = millis() + SW_ACK_WAIT_MS * 2;
        while (millis() < yieldEnd) {
            if (radio.available()) {
                uint8_t tmp[PAYLOAD_SIZE];
                radio.read(tmp, PAYLOAD_SIZE);
                uint8_t pt = tmp[0];
                if (pt == PKT_SWACK || pt == PKT_SWACK_YIELD ||
                    pt == PKT_XON   || pt == PKT_XOFF) {
                    handleRadioPacket(tmp);
                } else if (!pendingPktReady) {
                    memcpy(pendingPkt, tmp, PAYLOAD_SIZE);
                    pendingPktReady = true;
                }
                break;
            }
        }
        return;
    }

    if (txAvail() == 0) return;

    if (flowMode != 2) {
        // HWACK / transparent: fire and forget all buffered data
        while (txAvail() > 0) {
            uint8_t chunk[MAX_DATA]; uint8_t n = 0;
            while (n < MAX_DATA && txAvail() > 0) { int b = txPop(); if (b<0) break; chunk[n++]=(uint8_t)b; }
            if (n == 0) break;
            if (!sendPacket(PKT_DATA, chunk, n)) break;
        }
        return;
    }

    // SWFLOW stop-and-wait: build one packet
    uint8_t chunk[MAX_DATA]; uint8_t n = 0;
    while (n < MAX_DATA && txAvail() > 0) { int b = txPop(); if (b<0) break; chunk[n++]=(uint8_t)b; }
    if (n == 0) return;

    bool gotAck = false;
    for (uint8_t attempt = 0; attempt <= SW_RETX_MAX; attempt++) {
        if (attempt == 0) {
            sendPacket(PKT_DATA, chunk, n);   // stores copy in swLastPkt
        } else if (swLastPktValid) {
            swRetxCount++;
            openWritePipe(remoteMac);
            radio.write(swLastPkt, PAYLOAD_SIZE);
            ledFlashSD();
            openListenPipes();
        } else break;

        unsigned long waitEnd = millis() + SW_ACK_WAIT_MS;
        while (millis() < waitEnd) {
            if (radio.available()) {
                uint8_t tmp[PAYLOAD_SIZE];
                radio.read(tmp, PAYLOAD_SIZE);
                uint8_t pt = tmp[0];
                if (pt == PKT_SWACK || pt == PKT_SWACK_YIELD) {
                    handleRadioPacket(tmp);
                    gotAck = true;
                } else if (pt == PKT_XON || pt == PKT_XOFF) {
                    handleRadioPacket(tmp);
                } else if (pt == PKT_TEST_STOP) {
                    testStopFlag = true;   // signal test loop to exit
                    gotAck = true;         // exit retransmit loop cleanly
                } else if (pt == PKT_DATA &&
                           tmp[2] >= 9 &&
                           tmp[DATA_OFFSET + 8] == TEST_FLAG_STOP &&
                           readU32(tmp + DATA_OFFSET + 4) == TEST_MAGIC) {
                    // Stop-flagged data packet from RX — set flag directly
                    // instead of buffering in pendingPkt (which may be full).
                    testStopFlag = true;
                    gotAck = true;
                } else if (!pendingPktReady) {
                    memcpy(pendingPkt, tmp, PAYLOAD_SIZE);
                    pendingPktReady = true;
                }
                break;
            }
        }
        if (gotAck) break;
    }
}

// ── XON/XOFF management ──────────────────────────────────────────────────────
void checkFlowControl() {
    uint16_t used = rxAvail();

    // Tell host to stop/resume — only if XON/XOFF mode is enabled (S13=1)
    if (regS13 == 1) {
        if (!hostXoffSent && used >= XOFF_THRESHOLD) {
            Serial.write(0x13);  // XOFF
            hostXoffSent = true;
        }
        if (hostXoffSent && used <= XON_THRESHOLD) {
            Serial.write(0x11);  // XON
            hostXoffSent = false;
        }
    } else {
        hostXoffSent = false;  // S13=0: never assert XOFF to host
    }

    // Tell remote to stop — only when connected
    if (state == S_DATA || state == S_CONNECTED) {
        if (!radioXoffSent && used >= XOFF_THRESHOLD) {
            sendControlPacket(PKT_XOFF);
            radioXoffSent = true;
        }
        if (radioXoffSent && used <= XON_THRESHOLD) {
            sendControlPacket(PKT_XON);
            radioXoffSent = false;
        }
    }
}

// ── Process an incoming radio packet ─────────────────────────────────────────
void handleRadioPacket(const uint8_t *pkt) {
    uint8_t type = pkt[0];
    // uint8_t seq = pkt[1];   // could be used for duplicate detection
    uint8_t len  = pkt[2];

    switch (type) {
        case PKT_DATA:
            if (state == S_DATA) {
                // Data mode: accept and buffer received bytes
                ledFlashRD();
                if (swflowAckData(pkt[1])) {
                    for (uint8_t i = 0; i < len && i < MAX_DATA; i++) {
                        rxPush(pkt[DATA_OFFSET + i]);
                    }
                    checkFlowControl();
                }
            } else if (state == S_CONNECTED) {
                // CLI mode: remote may still be sending — ACK to keep
                // stop-and-wait flowing but discard data (not in data mode).
                swflowAckData(pkt[1]);
            }
            break;

        case PKT_CONN:
            // Incoming connection request — accept from IDLE or refresh if
            // already ringing (initiator may retransmit PKT_CONN after S7 timeout).
            if (state == S_IDLE || state == S_RINGING) {
                if (len >= 3) {
                    pendingMac[0] = pkt[DATA_OFFSET];
                    pendingMac[1] = pkt[DATA_OFFSET + 1];
                    pendingMac[2] = pkt[DATA_OFFSET + 2];
                } else {
                    memset(pendingMac, 0, 3);
                }
                lastConnMs = millis();
                if (state == S_IDLE) ringCount = 0;  // fresh call
                state = S_RINGING;
                // Print RING immediately on packet receipt — no cadence timer.
                // This ensures RING only appears when a real PKT_CONN arrives,
                // not on a timer that keeps firing after the caller is gone.
                sendRing();
                ringCount++;
                // Auto-answer if S0 threshold reached.
                if (regS0 > 0 && ringCount >= regS0) {
                    doAnswer();
                }
            }
            break;

        case PKT_DISC:
            if (state == S_DATA || state == S_CONNECTED) {
                lastDisconnectMs = millis();
                clearBuffers();
                kaInitiator   = false;
                kaMissed      = 0;
                kaWaitingPong = false;
                kaPingAt      = 0;
                kaLastPingMs  = 0;
                yieldToRemote  = false;
                swLastPktValid = false;
                rxLastSeq      = 0xFF;
                radioXoffRecv  = false;
                radioXoffSent  = false;
                hostXoffSent   = false;
                state = S_IDLE;
                openListenPipes();
                sendNoCarrier();
            }
            break;

        case PKT_XON:
            radioXoffRecv = false;
            break;

        case PKT_XOFF:
            radioXoffRecv = true;
            break;

        case PKT_ACK:
            // Handshake ACK — accept during active dial (S_CONNECTING) OR
            // during the inter-retry wait (S_IDLE with dialRetrying=true).
            // The second case handles manual ATA on the receiver while the
            // initiator is pausing between S8 retry attempts.
            if (state == S_CONNECTING ||
                (state == S_IDLE && dialRetrying && lastDialStr[0] != '\0')) {
                dialRetrying   = false;   // cancel any pending retry
                dialRetryCount = 0;
                clearBuffers();
                // Reset all SWFLOW sequence state for the new session.
                yieldToRemote  = false;
                swLastPktValid = false;
                rxLastSeq      = 0xFF;
                swRetxCount    = 0;
                testStopFlag   = false;
                kaInitiator   = true;
                kaMissed      = 0;
                kaWaitingPong = false;
                kaPingAt      = millis() + (unsigned long)regS11 * 1000UL;
                sendConnect();           // print while still in CLI mode
                state         = S_DATA;  // switch AFTER printing CONNECT
            }
            break;

        case PKT_NACK:
            // NACK not used in stop-and-wait mode; retransmit is handled
            // by the SW_ACK_WAIT timeout in flushTxBuffer.
            break;

        case PKT_PING:
            // Always reply regardless of S10 setting.
            sendControlPacket(PKT_PONG);
            // If S10=1 and we are the answerer, record arrival time for watchdog.
            if (regS10 && !kaInitiator)
                kaLastPingMs = millis();
            break;

        case PKT_PONG:
            kaMissed      = 0;
            kaWaitingPong = false;
            break;

        case PKT_DIAG_PING:
            // Diagnostic ping received — only reply if we are fully idle.
            // Print caller MAC (carried in payload bytes 0-2) and reply.
            if (state == S_IDLE) {
                Serial.print(F("\r\nPING from "));
                if (len >= 3) {
                    char macStr[7];
                    snprintf(macStr, sizeof(macStr), "%02X%02X%02X",
                             pkt[DATA_OFFSET], pkt[DATA_OFFSET+1], pkt[DATA_OFFSET+2]);
                    Serial.println(macStr);
                } else {
                    Serial.println(F("unknown"));
                }
                // Reply with PKT_DIAG_PONG carrying our own MAC
                uint8_t pong[PAYLOAD_SIZE];
                memset(pong, 0, PAYLOAD_SIZE);
                pong[0] = PKT_DIAG_PONG;
                pong[1] = txSeq++;
                pong[2] = 3;
                pong[DATA_OFFSET]   = ownMac[0];
                pong[DATA_OFFSET+1] = ownMac[1];
                pong[DATA_OFFSET+2] = ownMac[2];
                // Reply to sender's address (stored in pendingMac-style: use pkt source)
                // We need the sender MAC — it's in the ping payload
                if (len >= 3) {
                    uint8_t senderMac[3] = {pkt[DATA_OFFSET], pkt[DATA_OFFSET+1], pkt[DATA_OFFSET+2]};
                    openWritePipe(senderMac);
                    radio.write(pong, PAYLOAD_SIZE);
                    openListenPipes();
                }
            }
            break;

        case PKT_DIAG_PONG:
            // Handled inline in atPing() wait loop — nothing to do here.
            break;

        case PKT_SWACK_YIELD:
            // Remote ACKs our data AND wants a TX window.
            yieldToRemote = true;
            // fall through
        case PKT_SWACK:
            // Stop-and-wait: SWACK means our last packet was delivered.
            // swLastPkt is now stale — clear it.
            swLastPktValid = false;
            break;
    }
}

// ── SWFLOW receiver: send SWACK ─────────────────────────────────────────────
// Returns true if packet data should be accepted into rxBuf.
// Sends PKT_SWACK_YIELD if we have data queued (cooperative duplex),
// otherwise plain PKT_SWACK. No gap detection — stop-and-wait guarantees
// in-order delivery; duplicates detected by seq == last seen.
bool swflowAckData(uint8_t seq) {
    if (flowMode != 2) return true;

    // Duplicate detection: if seq matches rxLastSeq (global, reset on connect).
    if (rxLastSeq != 0xFF && seq == rxLastSeq) {
        // Re-send SWACK to unblock sender
        uint8_t ack[PAYLOAD_SIZE]; memset(ack, 0, PAYLOAD_SIZE);
        bool haveData = (txAvail() > 0) && !radioXoffRecv;
        ack[0] = haveData ? PKT_SWACK_YIELD : PKT_SWACK;
        ack[1] = txSeq++; ack[2] = 1; ack[DATA_OFFSET] = seq;
        openWritePipe(remoteMac); radio.write(ack, PAYLOAD_SIZE);
        if (haveData) ledFlashSD(); else ledFlashSD();
        openListenPipes();
        return false;   // discard duplicate
    }
    rxLastSeq = seq;

    // In-order: send SWACK (with yield if we have data)
    uint8_t ack[PAYLOAD_SIZE]; memset(ack, 0, PAYLOAD_SIZE);
    bool haveData = (txAvail() > 0) && !radioXoffRecv;
    ack[0] = haveData ? PKT_SWACK_YIELD : PKT_SWACK;
    ack[1] = txSeq++; ack[2] = 1; ack[DATA_OFFSET] = seq;
    openWritePipe(remoteMac);
    bool ok = radio.write(ack, PAYLOAD_SIZE);
    if (ok) ledFlashSD(); else ledFlashER();
    openListenPipes();
    return true;
}


// Complete an incoming call: copy pending MAC, ACK the caller, enter DATA mode.
void doAnswer() {
    memcpy(remoteMac, pendingMac, 3);
    // Only persist remoteMac — full saveConfig() on every call wastes EEPROM
    // write cycles. update() skips the write if value is unchanged.
    EEPROM.update(EE_REM_MAC,     remoteMac[0]);
    EEPROM.update(EE_REM_MAC + 1, remoteMac[1]);
    EEPROM.update(EE_REM_MAC + 2, remoteMac[2]);
    clearBuffers();                // discard any stale pre-connect data
    // Reset all SWFLOW sequence state for the new session.
    yieldToRemote  = false;
    swLastPktValid = false;
    rxLastSeq      = 0xFF;
    swRetxCount    = 0;
    testStopFlag   = false;
    sendConnectAck();
    kaInitiator   = false;  // we answered — remote owns keep-alive, we only reply
    kaMissed      = 0;
    kaWaitingPong = false;
    kaPingAt      = 0;      // answerer never initiates pings
    kaLastPingMs  = millis(); // arm watchdog from connect time
    sendConnect();           // print while still in CLI mode
    state         = S_DATA;  // switch to data mode AFTER printing CONNECT
}

void sendConnectAck() {
    sendControlPacket(PKT_ACK);
}

// ── Factory reset ────────────────────────────────────────────────────────────
// Restores every setting to its compile-time default and overwrites all EEPROM
// slots. The EEPROM magic byte is written last so a power-loss mid-write is
// safe — loadConfig() will see an invalid magic byte and stay at defaults.
void factoryReset() {
    // Radio / link settings
    ownMac[0]   = 0x01; ownMac[1]   = 0x02; ownMac[2]   = 0x03;
    remoteMac[0]= 0x00; remoteMac[1]= 0x00; remoteMac[2]= 0x00;
    channel     = 97;   // 2497 MHz — above all WiFi and Bluetooth
    speedEnum   = 1;
    flowMode    = 2;      // SWFLOW is default
    baudIdx     = BAUD_DEFAULT;

    // S-registers
    regS0 = 0;
    regS6 = 0;
    regS7 = 3;
    regS8  = 3;
    regS9  = 3;
    regS10 = 1;
    regS11 = 5;
    regS12 = 3;
    regS13 = 1;
    regS14 = 1;
    regS15 = 50;
    regS16 = 5;
    regS17 = 20;
    regS18 = 0;
    txDropped = 0;
    rxDropped = 0;

    // Terminal
    echoOn = true;

    // Dial profiles
    for (uint8_t s = 0; s < DIAL_SLOTS; s++)
        memset(dialStr[s], 0, DIAL_STR_LEN + 1);
    startupSlot = 0xFF;

    // Runtime state — cancel anything in progress
    dialRetryCount = 0;
    dialRetrying   = false;
    autoDial       = false;
    lastConnMs     = 0;
    memset(lastDialStr, 0, sizeof(lastDialStr));
    kaInitiator    = false;
    kaMissed       = 0;
    kaWaitingPong  = false;
    kaPingAt       = 0;
    kaLastPingMs   = 0;
    yieldToRemote  = false;
    swLastPktValid = false;
    rxLastSeq      = 0xFF;
    clearBuffers();

    // Persist — saveConfig() writes magic byte last
    saveConfig();

    // Re-apply radio config at new settings
    applyRadioConfig();
    openListenPipes();
}

// ── Spectrum scanner ─────────────────────────────────────────────────────────
// Sweeps all 126 nRF24L01 channels (0-125), sampling RPD for S17 (regS17) ms
// per channel, converts hit density to an ASCII character, and streams the
// result to serial. Prints a ruler header before each sweep. Runs until any
// serial byte is received.
//
// Density map (RPD hits out of ~regS17 samples):
//   0        → ' '  (no signal)
//   1–2      → '.'
//   3–4      → ':'
//   5–6      → '-'
//   7–8      → '='
//   9–10     → '+'
//   11–13    → '*'
//   14–16    → '#'
//   17–19    → '@'
//   20+      → '%'  (saturated)
//
// Ruler printed every sweep:
//   |         1         2  ...  (tens)
//   0123456789012345678901234...  (units)

// SPECTRUM_DWELL_MS replaced by S17 register (regS17) — see ATSn handler

void spectrumScan() {
    if (state != S_IDLE && state != S_CONNECTED) {
        Serial.print(F("\r\nERROR: disconnect first\r\n"));
        return;
    }

    Serial.println(F("SPECTRUM SCAN — any key to stop"));
    Serial.println(F("Ch:  0         1         2         3         4         5         6         7         8         9         10        11        12"));
    Serial.println(F("     0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456"));

    // Density to ASCII lookup
    static const char density[] = " .:-=+*#@%";
    //                              0 1  2  3  4  5  6  7  8  9+

    uint8_t sweepCount = 0;   // counts completed sweeps for footer interval

    while (true) {
        // Check for keypress — any byte stops the scan
        if (Serial.available()) {
            Serial.read();
            break;
        }

        Serial.print(F("     "));   // indent to align with ruler

        for (uint8_t ch = 0; ch < 126; ch++) {
            // Each channel: perform regS17 independent RPD measurements.
            // Each measurement: stop→start (resets latch) → settle 500µs → read.
            // 500µs is conservative but reliable for PLL lock + RPD valid.
            // With regS17=20 samples: ~10ms per channel, ~1.26s per sweep.
            // Increase regS17 for denser/more reliable readings.
            uint8_t hits = 0;
            for (uint8_t s = 0; s < regS17; s++) {
                radio.setChannel(ch);
                radio.stopListening();
                radio.startListening();      // resets RPD latch cleanly
                delayMicroseconds(500);      // PLL lock + RPD comparator valid
                if (radio.testRPD()) hits++;
            }

            // Map hits/regS17 percentage to density character:
            // 0%→' ', 1-12%→'.', 13-25%→':', 26-37%→'-', 38-50%→'=',
            // 51-62%→'+', 63-75%→'*', 76-87%→'#', 88-99%→'@', 100%→'%'
            uint8_t pct = (uint8_t)((uint16_t)hits * 100 / regS17);
            uint8_t idx = (pct == 0) ? 0 : (pct <= 12) ? 1 : (pct <= 25) ? 2 :
                          (pct <= 37) ? 3 : (pct <= 50) ? 4 : (pct <= 62) ? 5 :
                          (pct <= 75) ? 6 : (pct <= 87) ? 7 : (pct <= 99) ? 8 : 9;
            Serial.write(density[idx]);

            // Check for keypress between channels
            if (Serial.available()) {
                Serial.read();
                goto scan_done;
            }
        }
        Serial.println();   // end of sweep — CR+LF
        sweepCount++;
        // Print footer ruler every 40 sweeps so the scale is always visible.
        if (sweepCount >= 40) {
            sweepCount = 0;
            Serial.println(F("     0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456"));
            Serial.println(F("Ch:  0         1         2         3         4         5         6         7         8         9         10        11        12"));
            Serial.println(F("     0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456"));
        }
    }

scan_done:
    // Restore radio to configured channel and return to listen mode
    radio.setChannel(channel);
    applyRadioConfig();
    openListenPipes();
    Serial.println();
    sendOK();
}

// ── Channel busy detection ─────────────────────────────────────────────────────
// Takes N independent RPD samples on the current channel, each with a fresh
// startListening() call to reset the latch between samples.
// The RPD latch asserts when signal > -64 dBm and stays set until the next
// startListening(). Each sample is therefore a clean independent measurement.
// Returns true if the channel appears occupied, false if clear.
//
// Sample count = regS15 / 2 (each sample takes ~2 ms: 1.8 ms settle + read).
// With regS15=50 (default): 25 samples. Threshold: at least 2 hits required,
// preventing single-sample glitches from causing a false BUSY.
bool channelIsBusy() {
    // Grace period: skip busy detect for 200 ms after a disconnect so our own
    // PKT_DISC and the remote's lingering packets don't cause false BUSY.
    if (millis() - lastDisconnectMs < 200UL) {
        return false;
    }

    uint8_t samples = regS15 / 2;   // e.g. 25 samples at default S15=50
    if (samples < 3) samples = 3;   // minimum 3 for a meaningful result

    uint8_t hits = 0;
    for (uint8_t i = 0; i < samples; i++) {
        // Must cycle through standby to guarantee RPD latch resets.
        // Calling startListening() when already in RX mode is a no-op on
        // nRF24L01 — the latch from the last received packet stays set.
        // stopListening() → startListening() forces the full RX re-arm.
        radio.stopListening();
        delayMicroseconds(130);      // standby settle
        radio.startListening();
        delayMicroseconds(1800);     // RX settle + RPD comparator valid
        if (radio.testRPD()) hits++;
    }

    openListenPipes();

    // Require at least 2 hits to declare busy — prevents single noise spikes
    // from blocking a dial attempt on what is actually a clear channel.
    return (hits >= 2);
}

// ── Speed test ───────────────────────────────────────────────────────────────
// ATTEST-TX: flood PKT_DATA packets with embedded seq numbers to the remote.
// ATTEST-RX: wait for the magic signature, then count received bytes/packets.
//
// Test packet payload layout (29 bytes):
//   [0..3]  uint32_t sequence number (little-endian, starts at 0)
//   [4..7]  uint32_t magic 0xDEADBEEF  ← RX arms on first packet with this magic
//   [8..28] 0xAA fill
//
#define TEST_MAGIC     0xDEADBEEFUL
#define TEST_PAY       MAX_DATA       // 29 bytes per packet
#define TEST_STATS_MS  1000           // print stats every 1 s
#define TEST_FLAG_OK   0x00           // payload[8]: normal packet
#define TEST_FLAG_STOP 0xFF           // payload[8]: stop signal embedded in data

// Write a uint32 little-endian into a buffer
static void writeU32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static uint32_t readU32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

void speedTestTX() {
    if (state != S_CONNECTED) {
        Serial.print(F("\r\nERROR: must be in CLI mode (use +++ first)\r\n"));
        return;
    }
    Serial.print(F("\r\nATTEST-TX: sending — any key to stop\r\n"));

    uint32_t seqNum     = 0;
    uint32_t totalBytes = 0;
    uint32_t retxStart  = swRetxCount;
    uint32_t dropStart  = txDropped;
    unsigned long testStart = millis();
    unsigned long lastStats = testStart;

    state = S_DATA;

    // Send PKT_TEST_START so RX arms/re-arms cleanly
    sendControlPacket(PKT_TEST_START);

    while (true) {
        // Keypress — embed STOP flag in a final sequenced packet so it
        // travels through the same reliable data path as all other packets.
        if (Serial.available()) {
            Serial.read();
            uint8_t stopPay[TEST_PAY];
            writeU32(stopPay,     seqNum);
            writeU32(stopPay + 4, TEST_MAGIC);
            stopPay[8] = TEST_FLAG_STOP;
            memset(stopPay + 9, 0xAA, TEST_PAY - 9);
            for (uint8_t i = 0; i < TEST_PAY; i++) txPush(stopPay[i]);
            flushTxBuffer();
            break;
        }

        // Stop signal is detected inside flushTxBuffer's ACK wait loop
        // and sets testStopFlag — catches it even when pendingPkt is full.
        if (testStopFlag) {
            testStopFlag    = false;
            pendingPktReady = false;
            Serial.print(F("\r\n[TX] RX stopped the test\r\n"));
            break;
        }
        if (pendingPktReady) { pendingPktReady = false; }

        // Build and send normal test payload
        uint8_t payload[TEST_PAY];
        writeU32(payload,     seqNum);
        writeU32(payload + 4, TEST_MAGIC);
        payload[8] = TEST_FLAG_OK;
        memset(payload + 9, 0xAA, TEST_PAY - 9);
        for (uint8_t i = 0; i < TEST_PAY; i++) txPush(payload[i]);
        flushTxBuffer();
        totalBytes += TEST_PAY;
        seqNum++;

        // testStopFlag checked at loop top covers this case

        unsigned long now = millis();
        if (now - lastStats >= TEST_STATS_MS) {
            unsigned long elapsed = (now - testStart) / 1000UL;
            uint32_t speed = elapsed ? (totalBytes / elapsed) : totalBytes;
            uint32_t retx = swRetxCount - retxStart;
            uint32_t retxPct = seqNum ? (retx * 100 / seqNum) : 0;
            Serial.print(F("[TX] t="));    Serial.print(elapsed);
            Serial.print(F("s  pkts="));   Serial.print(seqNum);
            Serial.print(F("  speed="));   Serial.print(speed);
            Serial.print(F(" Payload B/s  retx=")); Serial.print(retx);
            Serial.print(F(" (")); Serial.print(retxPct); Serial.print(F("%)"));
            Serial.print(F("  drop="));    Serial.println(txDropped - dropStart);
            lastStats = now;
        }
    }

tx_done:
    {
        unsigned long elapsed = millis() - testStart;
        uint32_t speed = elapsed ? (totalBytes * 1000UL / elapsed) : 0;
        uint32_t retx = swRetxCount - retxStart;
        uint32_t retxPct = seqNum ? (retx * 100 / seqNum) : 0;
        Serial.print(F("\r\n[TX DONE] pkts=")); Serial.print(seqNum);
        Serial.print(F("  speed="));   Serial.print(speed);
        Serial.print(F(" Payload B/s  retx=")); Serial.print(retx);
        Serial.print(F(" (")); Serial.print(retxPct); Serial.print(F("%)"));
        Serial.print(F("  drop="));    Serial.println(txDropped - dropStart);
    }
    pendingPktReady = false;
    testStopFlag    = false;
    // Reset KA timers so keep-alive resumes cleanly.
    kaPingAt     = millis() + (unsigned long)regS11 * 1000UL;
    kaLastPingMs = millis();
    kaMissed     = 0;
    kaWaitingPong = false;
    state = S_CONNECTED;
    sendOK();
}

void speedTestRX() {
    if (state != S_CONNECTED) {
        Serial.print(F("\r\nERROR: must be in CLI mode (use +++ first)\r\n"));
        return;
    }
    Serial.print(F("\r\nATTEST-RX: waiting for test stream — any key to stop\r\n"));

    bool     armed       = false;
    uint32_t expectedSeq = 0;
    uint32_t totalBytes  = 0;
    uint32_t lostPkts    = 0;
    uint32_t dupPkts     = 0;
    uint32_t recvPkts    = 0;
    uint32_t maxBurst    = 0;   // largest consecutive loss run seen
    uint32_t dropStart   = rxDropped;
    unsigned long testStart = 0;
    unsigned long lastStats = 0;

    state = S_DATA;

    while (true) {
        // Keypress — send a stop-flagged data packet to TX so it exits cleanly.
        // Using a PKT_DATA packet with TEST_FLAG_STOP means it travels the
        // same reliable stop-and-wait path as all other packets.
        if (Serial.available()) {
            Serial.read();
            uint8_t stopPay[TEST_PAY];
            writeU32(stopPay,     0xFFFFFFFFUL);  // unmistakable seq sentinel
            writeU32(stopPay + 4, TEST_MAGIC);
            stopPay[8] = TEST_FLAG_STOP;
            memset(stopPay + 9, 0xAA, TEST_PAY - 9);
            for (uint8_t i = 0; i < TEST_PAY; i++) txPush(stopPay[i]);
            flushTxBuffer();
            break;
        }

        unsigned long now = millis();

        // Drain pending packet — check for stop flag first
        if (pendingPktReady) {
            pendingPktReady = false;
            if (pendingPkt[0] == PKT_DATA &&
                pendingPkt[2] >= 9 &&
                pendingPkt[DATA_OFFSET + 8] == TEST_FLAG_STOP &&
                readU32(pendingPkt + DATA_OFFSET + 4) == TEST_MAGIC) {
                Serial.print(F("\r\n[RX] TX stop flag received\r\n"));
                goto rx_done;
            } else if (pendingPkt[0] != PKT_TEST_START &&
                       pendingPkt[0] != PKT_TEST_STOP) {
                handleRadioPacket(pendingPkt);
            }
        }

        if (radio.available()) {
            uint8_t pkt[PAYLOAD_SIZE];
            radio.read(pkt, PAYLOAD_SIZE);
            uint8_t pt = pkt[0];

            if (pt == PKT_TEST_STOP) {
                // TX stopped the test
                Serial.print(F("\r\n[RX] TX stopped the test\r\n"));
                goto rx_done;
            }

            if (pt == PKT_TEST_START) {
                // TX starting or restarting — re-arm cleanly
                if (armed) {
                    Serial.print(F("\r\n[RX] TX restarted — re-arming\r\n"));
                }
                armed       = false;
                expectedSeq = 0;
                totalBytes  = 0;
                lostPkts    = 0;
                dupPkts     = 0;
                recvPkts    = 0;
                maxBurst    = 0;
                dropStart   = rxDropped;
                testStart   = 0;
                lastStats   = 0;
                continue;
            }

            if (pt == PKT_DATA) {
                uint8_t *pay = pkt + DATA_OFFSET;
                uint32_t seq   = readU32(pay);
                uint32_t magic = readU32(pay + 4);
                uint8_t  flag  = (pkt[2] >= 9) ? pay[8] : TEST_FLAG_OK;

                if (!armed) {
                    if (magic == TEST_MAGIC) {
                        armed       = true;
                        expectedSeq = seq;
                        testStart   = millis();
                        lastStats   = testStart;
                        Serial.print(F("[RX] armed on seq="));
                        Serial.println(seq);
                        swflowAckData(pkt[1]);
                    }
                } else {
                    // Count packet before checking stop flag
                    if (seq == expectedSeq) {
                        expectedSeq++;
                        totalBytes += TEST_PAY;
                        recvPkts++;
                    } else if (seq > expectedSeq) {
                        uint32_t gap = seq - expectedSeq;
                        lostPkts   += gap;
                        if (gap > maxBurst) maxBurst = gap;
                        expectedSeq = seq + 1;
                        totalBytes += TEST_PAY;
                        recvPkts++;
                    } else {
                        dupPkts++;
                    }
                    swflowAckData(pkt[1]);
                    // Check stop flag AFTER ACKing so TX gets confirmation
                    if (flag == TEST_FLAG_STOP) {
                        Serial.print(F("\r\n[RX] TX stop flag received\r\n"));
                        goto rx_done;
                    }
                }
            } else {
                handleRadioPacket(pkt);
            }
        }

        if (armed && (now - lastStats >= TEST_STATS_MS)) {
            unsigned long elapsed = (now - testStart) / 1000UL;
            uint32_t speed = elapsed ? (totalBytes / elapsed) : totalBytes;
            uint32_t total = recvPkts + lostPkts;
            uint32_t pdr   = total ? (recvPkts * 100 / total) : 100;
            Serial.print(F("[RX] t="));    Serial.print(elapsed);
            Serial.print(F("s  pkts="));   Serial.print(recvPkts);
            Serial.print(F("  speed="));   Serial.print(speed);
            Serial.print(F(" Payload B/s  PDR="));  Serial.print(pdr);
            Serial.print(F("%  lost="));   Serial.print(lostPkts);
            Serial.print(F("  burst="));   Serial.print(maxBurst);
            Serial.print(F("  dup="));      Serial.print(dupPkts);
            Serial.print(F("  drop="));     Serial.println(rxDropped - dropStart);
            lastStats = now;
        }
    }

rx_done:
    if (armed) {
        unsigned long elapsed = millis() - testStart;
        uint32_t speed = elapsed ? (totalBytes * 1000UL / elapsed) : 0;
        uint32_t total = recvPkts + lostPkts;
        uint32_t pdr   = total ? (recvPkts * 100 / total) : 100;
        Serial.print(F("\r\n[RX DONE] pkts=")); Serial.print(recvPkts);
        Serial.print(F("  speed="));   Serial.print(speed);
        Serial.print(F(" Payload B/s  PDR="));  Serial.print(pdr);
        Serial.print(F("%  lost="));   Serial.print(lostPkts);
        Serial.print(F("  burst="));   Serial.print(maxBurst);
        Serial.print(F("  dup="));      Serial.print(dupPkts);
        Serial.print(F("  drop="));     Serial.println(rxDropped - dropStart);
    } else {
        Serial.print(F("\r\n[RX] no test stream received\r\n"));
    }
    pendingPktReady = false;
    clearBuffers();
    // Reset KA timers so keep-alive resumes cleanly.
    kaPingAt     = millis() + (unsigned long)regS11 * 1000UL;
    kaLastPingMs = millis();
    kaMissed     = 0;
    kaWaitingPong = false;
    state = S_CONNECTED;
    sendOK();
}

// ── Speed test echo ──────────────────────────────────────────────────────────
// ATTEST-ECHO: reflects every received PKT_DATA packet back to sender.
// Pair with ATTEST-TX on the other side. Any key stops. No stop signal
// sent to remote — TX side just sees retx rise when echo stops replying.
void speedTestEcho() {
    if (state != S_CONNECTED) {
        Serial.print(F("\r\nERROR: must be in CLI mode (use +++ first)\r\n"));
        return;
    }
    Serial.print(F("\r\nATTEST-ECHO: reflecting packets — any key to stop\r\n"));

    uint32_t echoCount  = 0;
    uint32_t dropStart  = rxDropped;
    unsigned long echoStart = millis();
    unsigned long lastStats = echoStart;

    state = S_DATA;

    while (true) {
        if (Serial.available()) { Serial.read(); break; }

        // Drain pending packet first
        if (pendingPktReady) {
            pendingPktReady = false;
            handleRadioPacket(pendingPkt);
        }

        if (radio.available()) {
            uint8_t pkt[PAYLOAD_SIZE];
            radio.read(pkt, PAYLOAD_SIZE);

            if (pkt[0] == PKT_DATA) {
                uint8_t len = pkt[2];
                // Push payload bytes into txBuf to echo back
                for (uint8_t i = 0; i < len && i < MAX_DATA; i++) {
                    txPush(pkt[DATA_OFFSET + i]);
                }
                // Send SWACK_YIELD — we have data to send (the echo)
                swflowAckData(pkt[1]);
                // Flush the echo immediately
                flushTxBuffer();
                echoCount++;
            } else {
                handleRadioPacket(pkt);
            }
        }

        unsigned long now = millis();
        if (now - lastStats >= TEST_STATS_MS) {
            unsigned long elapsed = (now - echoStart) / 1000UL;
            uint32_t speed = elapsed ? (echoCount * (TEST_PAY - 9) / elapsed) : 0;
            Serial.print(F("[ECHO] t="));    Serial.print(elapsed);
            Serial.print(F("s  pkts="));     Serial.print(echoCount);
            Serial.print(F("  speed="));     Serial.print(speed);
            Serial.print(F(" Payload B/s  drop=")); Serial.println(rxDropped - dropStart);
            lastStats = now;
        }
    }

    unsigned long elapsed = millis() - echoStart;
    uint32_t speed = elapsed ? (echoCount * (TEST_PAY - 9) * 1000UL / elapsed) : 0;
    Serial.print(F("\r\n[ECHO DONE] pkts=")); Serial.print(echoCount);
    Serial.print(F("  speed="));   Serial.print(speed);
    Serial.print(F(" Payload B/s  drop=")); Serial.println(rxDropped - dropStart);

    // Reset KA timers and return to CLI
    pendingPktReady = false;
    clearBuffers();
    kaPingAt      = millis() + (unsigned long)regS11 * 1000UL;
    kaLastPingMs  = millis();
    kaMissed      = 0;
    kaWaitingPong = false;
    state = S_CONNECTED;
    sendOK();
}

// ── Diagnostic ping ──────────────────────────────────────────────────────────
// Sends PKT_DIAG_PING to the target MAC and waits S7 seconds for PKT_DIAG_PONG.
// Only callable from S_IDLE — no active or pending connection.
void atPing(const uint8_t targetMac[3]) {
    // Send PKT_DIAG_PING carrying our own MAC as payload
    uint8_t ping[PAYLOAD_SIZE];
    memset(ping, 0, PAYLOAD_SIZE);
    ping[0] = PKT_DIAG_PING;
    ping[1] = txSeq++;
    ping[2] = 3;
    ping[DATA_OFFSET]   = ownMac[0];
    ping[DATA_OFFSET+1] = ownMac[1];
    ping[DATA_OFFSET+2] = ownMac[2];

    openWritePipe(targetMac);
    bool sent = radio.write(ping, PAYLOAD_SIZE);
    openListenPipes();

    if (!sent) {
        Serial.print(F("\r\nNO RESPONSE\r\n"));
        sendOK();
        return;
    }

    // Wait up to S7 seconds for PKT_DIAG_PONG
    unsigned long deadline = millis() + (unsigned long)regS7 * 1000UL;
    while (millis() < deadline) {
        if (radio.available()) {
            uint8_t pkt[PAYLOAD_SIZE];
            radio.read(pkt, PAYLOAD_SIZE);
            if (pkt[0] == PKT_DIAG_PONG) {
                uint8_t len = pkt[2];
                Serial.print(F("\r\nPONG from "));
                if (len >= 3) {
                    char macStr[7];
                    snprintf(macStr, sizeof(macStr), "%02X%02X%02X",
                             pkt[DATA_OFFSET], pkt[DATA_OFFSET+1], pkt[DATA_OFFSET+2]);
                    Serial.println(macStr);
                } else {
                    Serial.println(F("unknown"));
                }
                sendOK();
                return;
            }
            // Ignore anything else (stray packets on channel)
        }
    }
    Serial.print(F("\r\nNO RESPONSE\r\n"));
    sendOK();
}

// ── Command parser ────────────────────────────────────────────────────────────
bool parseMac(const char *str, uint8_t mac[3]) {
    // Expects exactly 6 hex digits, e.g. "A1B2C3"
    char tmp[7];
    strncpy(tmp, str, 6);
    tmp[6] = '\0';
    for (uint8_t i = 0; i < 6; i++) {
        if (!isHexadecimalDigit(tmp[i])) return false;
    }
    // Parse each byte from two hex digit characters.
    // hexVal: convert single hex char '0'-'9','A'-'F' to 0-15.
    auto hexVal = [](char c) -> uint8_t {
        return (c >= '0' && c <= '9') ? (c - '0') : (c - 'A' + 10);
    };
    mac[0] = (hexVal(tmp[0]) << 4) | hexVal(tmp[1]);
    mac[1] = (hexVal(tmp[2]) << 4) | hexVal(tmp[3]);
    mac[2] = (hexVal(tmp[4]) << 4) | hexVal(tmp[5]);
    return true;
}

void printMac(const uint8_t mac[3]) {
    char buf[7];
    snprintf(buf, sizeof(buf), "%02X%02X%02X", mac[0], mac[1], mac[2]);
    Serial.print(buf);
}

void processCommand(const char *cmd) {
    // Normalise: strip leading whitespace, upper-case copy
    while (*cmd == ' ') cmd++;
    // S18 silent mode: ATS18= and AT&F are always allowed (escape hatch).
    // All other commands still execute for state transitions — output
    // helpers (sendOK, sendError, cliPrint etc.) are gated by regS18.
    (void)0;  // output suppression handled in helpers
    char uc[CMD_BUF_SIZE];
    uint8_t i = 0;
    while (cmd[i] && i < CMD_BUF_SIZE - 1) {
        uc[i] = toupper((unsigned char)cmd[i]);
        i++;
    }
    uc[i] = '\0';

    // ── ATI ─────────────────────────────────────────────────────────────────
    if (strcmp(uc, "ATI") == 0) {
        if (regS18) { sendOK(); return; }   // silent mode: suppress ATI output
        if (regS18) { sendOK(); return; }   // silent mode: suppress ATI output
        Serial.print(F("\r\nnRF24L01 AT Modem "));
        Serial.println(F(MODEM_VERSION));

        // Radio hardware status — re-check live so ATI always reflects reality.
        bool chipOk = radio.isChipConnected();
        if (radioFailed || !chipOk) {
            Serial.println(F("Radio   : FAULT - module not responding"));
        } else {
            Serial.println(F("Radio   : OK"));
        }

        Serial.print(F("Baud    : ")); Serial.println(baudTable[baudIdx]);
        Serial.print(F("State   : "));
        switch (state) {
            case S_IDLE:       Serial.println(F("IDLE"));                break;
            case S_RINGING:    Serial.println(F("RINGING"));             break;
            case S_CONNECTING: Serial.println(F("CONNECTING (dialling)")); break;
            case S_CONNECTED:  Serial.println(F("CONNECTED (CLI mode)")); break;
            case S_DATA:       Serial.println(F("CONNECTED (data mode)")); break;
            default:           Serial.println(F("UNKNOWN"));             break;
        }
        Serial.print(F("Flow mode: "));
        if      (flowMode == 0) Serial.println(F("0 (transparent - no framing)"));
        else if (flowMode == 1) Serial.println(F("1 (HWACK - hardware ACK)"));
        else                    Serial.println(F("2 (SWFLOW - software flow control)"));
        Serial.print(F("Channel : ")); Serial.println(channel);
        Serial.print(F("Speed   : "));
        if      (speedEnum == 0) Serial.println(F("250 kbps"));
        else if (speedEnum == 2) Serial.println(F("2 Mbps"));
        else                     Serial.println(F("1 Mbps"));
        Serial.print(F("Own MAC : ")); printMac(ownMac);  Serial.println();
        Serial.print(F("Rem MAC : ")); printMac(remoteMac); Serial.println();

        // Only attempt RSSI scan if the radio is healthy.
        if (!radioFailed && chipOk) {
            Serial.print(F("RSSI    : ")); Serial.print(readRSSI()); Serial.println(F(" dBm (approx)"));
        } else {
            Serial.println(F("RSSI    : N/A"));
        }

        Serial.print(F("S6      : ")); Serial.print(regS6); Serial.println(F(" s  (pre-dial wait, 0=none)"));
        Serial.print(F("S7      : ")); Serial.print(regS7); Serial.println(F(" s  (carrier timeout)"));
        Serial.print(F("S8      : "));
        if (regS8 == 255) Serial.println(F("255  (retry forever)"));
        else { Serial.print(regS8); Serial.println(F("    (retry attempts)")); }
        Serial.print(F("S9      : ")); Serial.print(regS9);  Serial.println(F(" s  (retry interval)"));
        Serial.print(F("S10     : ")); Serial.print(regS10); Serial.println(regS10 ? F("    (keep-alive ON)") : F("    (keep-alive OFF)"));
        Serial.print(F("S11     : ")); Serial.print(regS11); Serial.println(F(" s  (keep-alive interval)"));
        Serial.print(F("S12     : ")); Serial.print(regS12); Serial.println(F("    (missed pongs before drop)"));
        Serial.print(F("S13     : ")); Serial.println(regS13 == 1 ? F("1 (XON/XOFF enabled)") : F("0 (no flow control)"));
        Serial.print(F("S14     : ")); Serial.println(regS14 == 1 ? F("1 (busy detect ON)") : F("0 (busy detect OFF)"));
        Serial.print(F("S15     : ")); Serial.print(regS15); Serial.println(F(" ms (channel scan duration)"));
        Serial.print(F("S16     : ")); Serial.print(regS16); Serial.println(F(" ms (transparent TX idle flush)"));
        Serial.print(F("S17     : ")); Serial.print(regS17); Serial.println(F(" ms (spectrum scan dwell per channel)"));
        Serial.print(F("S18     : ")); Serial.println(regS18 == 1 ? F("1 (silent mode ON)") : F("0 (normal output)"));
        Serial.print(F("S18     : ")); Serial.println(regS18 == 1 ? F("1 (silent mode ON)") : F("0 (normal output)"));
        Serial.print(F("TX drop : ")); Serial.print(txDropped); Serial.println(F(" bytes (serial->radio, host overflow)"));
        Serial.print(F("RX drop : ")); Serial.print(rxDropped); Serial.println(F(" bytes (radio->serial, radio overflow)"));
        if (txDropped || rxDropped) Serial.println(F("** DATA LOSS DETECTED — consider enabling XON/XOFF (ATS13=1) **"));
        txDropped = 0;  // reset after display
        rxDropped = 0;
        if (state == S_DATA || state == S_CONNECTED) {
            Serial.print(F("KA role : ")); Serial.println(kaInitiator ? F("initiator (sends pings)") : F("responder (replies only)"));
        }
        if (regS10 && kaInitiator && (state == S_DATA || state == S_CONNECTED)) {
            Serial.print(F("KA miss : ")); Serial.println(kaMissed);
        }
        if (regS10 && !kaInitiator && (state == S_DATA || state == S_CONNECTED)) {
            if (kaLastPingMs != 0) {
                unsigned long ageSec = (millis() - kaLastPingMs) / 1000UL;
                unsigned long windowSec = (unsigned long)regS11 * regS12;
                Serial.print(F("Last ping: ")); Serial.print(ageSec);
                Serial.print(F(" s ago (timeout "));
                Serial.print(windowSec);
                Serial.println(F(" s)"));
            } else {
                Serial.println(F("Last ping: none yet"));
            }
        }
        Serial.print(F("Startup : "));
        if (startupSlot < DIAL_SLOTS && dialStr[startupSlot][0] != '\0') {
            Serial.print(startupSlot);
            Serial.print(F(" -> "));
            Serial.println(dialStr[startupSlot]);
        } else {
            Serial.println(F("none"));
        }
        for (uint8_t s = 0; s < DIAL_SLOTS; s++) {
            Serial.print(F("&Z"));
            Serial.print(s);
            Serial.print(F("     : "));
            Serial.println(dialStr[s][0] ? dialStr[s] : "(empty)");
        }
        sendOK();
        return;
    }

    // ── ATO — return to data mode after +++ escape ───────────────────────────
    if (strcmp(uc, "ATO") == 0 || strcmp(uc, "ATO0") == 0) {
        if (state == S_CONNECTED) {
            state = S_DATA;
            sendConnect();   // Hayes standard: ATO confirms with CONNECT
        } else if (state == S_DATA) {
            sendConnect();   // already in data mode
        } else {
            sendError();     // no active connection to return to
        }
        return;
    }

    // ── ATE0 / ATE1 ─────────────────────────────────────────────────────────
    if (strcmp(uc, "ATE0") == 0) { echoOn = false; sendOK(); return; }
    if (strcmp(uc, "ATE1") == 0) { echoOn = true;  sendOK(); return; }

    // ── ATH ─────────────────────────────────────────────────────────────────
    if (strcmp(uc, "ATH") == 0 || strcmp(uc, "ATH0") == 0) {
        lastDisconnectMs = millis();
        pendingPktReady  = false;
        radioXoffRecv    = false;
        radioXoffSent    = false;
        hostXoffSent     = false;
        // Clear SWFLOW window, buffers, and cancel any pending retry on hangup.
        clearBuffers();
        transBufLen = 0;
        dialRetrying   = false;
        dialRetryCount = 0;
        kaInitiator    = false;
        kaMissed       = 0;
        kaWaitingPong  = false;
        kaPingAt       = 0;
        kaLastPingMs   = 0;
        yieldToRemote  = false;
        swLastPktValid = false;
        rxLastSeq      = 0xFF;
        if (state == S_RINGING) {
            // Reject incoming call — send DISC back to caller using pending MAC.
            memcpy(remoteMac, pendingMac, 3);
            for (uint8_t _d = 0; _d < 3; _d++) { sendControlPacket(PKT_DISC); delay(10); }
            memset(remoteMac, 0, 3);
            ringCount  = 0;
            lastConnMs = 0;
            state = S_IDLE;
            openListenPipes();
        } else if (state == S_DATA || state == S_CONNECTED || state == S_CONNECTING) {
            for (uint8_t _d = 0; _d < 3; _d++) { sendControlPacket(PKT_DISC); delay(10); }
            state = S_IDLE;
            openListenPipes();
            sendNoCarrier();   // dropped active connection
            return;
        }
        sendOK();   // was already idle or just rejected a ring
        return;
    }

    // ── ATSMYMAC? / ATSMYMAC=XXYYZZ ─────────────────────────────────────────
    if (strcmp(uc, "ATSMYMAC?") == 0) {
        printMac(ownMac); Serial.println();
        sendOK();
        return;
    }
    if (strncmp(uc, "ATSMYMAC=", 9) == 0) {
        uint8_t mac[3];
        if (parseMac(uc + 9, mac)) {
            memcpy(ownMac, mac, 3);
            saveConfig();
            applyRadioConfig();
            openListenPipes();
            sendOK();
        } else {
            sendError();
        }
        return;
    }

    // ── ATSETCH? / ATSETCH=nn ──────────────────────────────────────────────
    if (strcmp(uc, "ATSETCH?") == 0) {
        Serial.println(channel);
        sendOK();
        return;
    }
    if (strncmp(uc, "ATSETCH=", 8) == 0) {
        int ch = atoi(uc + 8);
        if (ch >= 0 && ch <= 125) {
            channel = (uint8_t)ch;
            saveConfig();
            applyRadioConfig();
            openListenPipes();
            sendOK();
        } else {
            sendError();
        }
        return;
    }

    // ── ATSSPEED? / ATSSPEED=n ─────────────────────────────────────────────
    if (strcmp(uc, "ATSSPEED?") == 0) {
        if      (speedEnum == 0) Serial.println(F("0 (250 kbps)"));
        else if (speedEnum == 2) Serial.println(F("2 (2 Mbps)"));
        else                     Serial.println(F("1 (1 Mbps)"));
        sendOK();
        return;
    }
    if (strncmp(uc, "ATSSPEED=", 9) == 0) {
        int sp = atoi(uc + 9);
        if (sp >= 0 && sp <= 2) {
            speedEnum = (uint8_t)sp;
            saveConfig();
            applyRadioConfig();
            openListenPipes();
            sendOK();
        } else {
            sendError();
        }
        return;
    }

    // ── ATD XXYYZZ ──────────────────────────────────────────────────────────
    if (strncmp(uc, "ATD", 3) == 0) {
        const char *macStr = uc + 3;
        while (*macStr == ' ') macStr++;
        uint8_t mac[3];
        if (parseMac(macStr, mac)) {
            memcpy(remoteMac, mac, 3);
            saveConfig();

            // Save full command for retry re-execution.
            strncpy(lastDialStr, cmd, sizeof(lastDialStr) - 1);
            lastDialStr[sizeof(lastDialStr) - 1] = '\0';

            // Transparent mode (flowMode=0): instant connect, no handshake.
            if (flowMode == 0) {
                clearBuffers();
                transBufLen   = 0;
                transLastByteMs = 0;
                state = S_DATA;
                sendConnect();
                return;
            }

            // S6: pre-dial wait — non-blocking so serial/LEDs stay responsive.
            if (regS6 > 0) {
                unsigned long waitEnd = millis() + (unsigned long)regS6 * 1000UL;
                while (millis() < waitEnd) {
                    updateSteadyLEDs();
                    updateFlashLEDs();
                    lastSerialMs = millis(); // suppress TR flicker
                }
            }

            // S14: busy detect — scan channel before dialling.
            if (regS14 == 1) {
                if (channelIsBusy()) {
                    Serial.print(F("\r\nBUSY\r\n"));
                    ledFlashER();
                    return;
                }
            }

            state = S_CONNECTING;

            // Send CONN packet carrying our own MAC as payload.
            uint8_t pkt[PAYLOAD_SIZE];
            memset(pkt, 0, PAYLOAD_SIZE);
            pkt[0] = PKT_CONN;
            pkt[1] = txSeq++;
            pkt[2] = 3;
            pkt[DATA_OFFSET]   = ownMac[0];
            pkt[DATA_OFFSET+1] = ownMac[1];
            pkt[DATA_OFFSET+2] = ownMac[2];
            openWritePipe(remoteMac);
            bool sent = radio.write(pkt, PAYLOAD_SIZE);
            openListenPipes();

            if (!sent) {
                // radio.write() failed (HWACK: all HW retries exhausted; no receiver
                // present). Don't bail immediately — leave state as S_CONNECTING and
                // let the S7 timeout in loop() handle it. That way S8 retries and
                // S9 intervals work correctly even when the remote is absent.
                // connectStart will be set to now on the next loop() iteration.
            }
            // Carrier wait timeout (S7) and retries (S8/S9) handled in loop().
        } else {
            sendError();
        }
        return;
    }

    // ── ATA ─────────────────────────────────────────────────────────────────
    if (strcmp(uc, "ATA") == 0) {
        if (state == S_RINGING) {
            doAnswer();
        } else {
            sendError();
        }
        return;
    }

    // ── ATSPECTRUM ──────────────────────────────────────────────────────────
    // Must be checked BEFORE the generic ATSn handler — "ATSPECTRUM" starts
    // with "ATS" and would otherwise be caught by the strncmp(uc,"ATS",3) check.
    if (strcmp(uc, "ATSPECTRUM") == 0) {
        spectrumScan();
        return;
    }

    // ── ATSn= / ATSn? — generic S-register handler ────────────────────────
    // Supported: S0 (auto-answer), S6 (pre-dial wait), S7 (carrier wait),
    //            S8 (retry count), S9 (retry interval seconds).
    if (strncmp(uc, "ATS", 3) == 0 && cmdLen >= 4) {
        uint8_t reg = (uint8_t)atoi(uc + 3);
        // Find the '=' or '?' character after the register number.
        const char *p = uc + 3;
        while (*p && isDigit(*p)) p++;
        bool isQuery = (*p == '?');
        bool isSet   = (*p == '=');

        if (!isQuery && !isSet) { sendError(); return; }

        // Map register number to its variable.
        uint8_t *regPtr = nullptr;
        uint8_t  regMax = 255;
        switch (reg) {
            case 0: regPtr = &regS0; break;
            case 6: regPtr = &regS6; break;
            case 7: regPtr = &regS7; break;
            case 8: regPtr = &regS8; break;
            case 9:  regPtr = &regS9;  break;
            case 10: regPtr = &regS10; break;
            case 11: regPtr = &regS11; break;
            case 12: regPtr = &regS12; break;
            case 13: regPtr = &regS13; break;
            case 14: regPtr = &regS14; break;
            case 15: regPtr = &regS15; break;
            case 16: regPtr = &regS16; break;
            case 17: regPtr = &regS17; break;
            case 18: regPtr = &regS18; break;
            default: sendError(); return;
        }

        if (isQuery) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%03u", *regPtr);
            Serial.println(buf);
            sendOK();
        } else {
            int val = atoi(p + 1);
            if (val >= 0 && val <= regMax) {
                *regPtr = (uint8_t)val;
                saveConfig();
                sendOK();
            } else {
                sendError();
            }
        }
        return;
    }

    // ── AT&Zn=string / AT&Zn? ──────────────────────────────────────────────
    if (strncmp(uc, "AT&Z", 4) == 0 && cmdLen >= 5) {
        uint8_t slot = (uint8_t)(uc[4] - '0');
        if (slot >= DIAL_SLOTS) { sendError(); return; }
        if (uc[5] == '?') {
            Serial.println(dialStr[slot][0] ? dialStr[slot] : "(empty)");
            sendOK();
        } else if (uc[5] == '=') {
            // Use original cmd pointer (not uc) to preserve case in stored string.
            const char *val = cmd + 6;
            while (*val == ' ') val++;
            uint8_t vlen = strlen(val);
            if (vlen > DIAL_STR_LEN) { sendError(); return; }
            strncpy(dialStr[slot], val, DIAL_STR_LEN);
            dialStr[slot][DIAL_STR_LEN] = '\0';
            saveConfig();
            sendOK();
        } else {
            sendError();
        }
        return;
    }

    // ── AT&Yn / AT&Y? ───────────────────────────────────────────────────────
    if (strncmp(uc, "AT&Y", 4) == 0 && cmdLen >= 5) {
        if (uc[4] == '?') {
            if (startupSlot < DIAL_SLOTS) {
                Serial.println(startupSlot);
            } else {
                Serial.println(F("none"));
            }
            sendOK();
        } else {
            uint8_t slot = (uint8_t)(uc[4] - '0');
            startupSlot = (slot < DIAL_SLOTS) ? slot : 0xFF;
            saveConfig();
            sendOK();
        }
        return;
    }

    // ── ATSBAUD? / ATSBAUD=n ───────────────────────────────────────────────
    if (strcmp(uc, "ATSBAUD?") == 0) {
        Serial.println(baudTable[baudIdx]);
        sendOK();
        return;
    }
    if (strncmp(uc, "ATSBAUD=", 8) == 0) {
        uint32_t requested = (uint32_t)atol(uc + 8);
        int8_t found = -1;
        for (uint8_t i = 0; i < BAUD_TABLE_SIZE; i++) {
            if (baudTable[i] == requested) { found = (int8_t)i; break; }
        }
        if (found >= 0) {
            baudIdx = (uint8_t)found;
            saveConfig();
            // Send OK at the CURRENT baud so the host sees the confirmation,
            // then switch rate after the TX buffer drains.
            sendOK();
            Serial.flush();             // block until all bytes are out
            Serial.begin(baudTable[baudIdx]);
        } else {
            // Print valid options then ERROR.
            Serial.print(F("Valid: "));
            for (uint8_t i = 0; i < BAUD_TABLE_SIZE; i++) {
                Serial.print(baudTable[i]);
                if (i < BAUD_TABLE_SIZE - 1) Serial.print(',');
            }
            Serial.println();
            sendError();
        }
        return;
    }

    // ── ATSFLOW? / ATSFLOW=n ────────────────────────────────────────────────
    if (strcmp(uc, "ATSFLOW?") == 0) {
        if      (flowMode == 0) Serial.println(F("0 (transparent)"));
        else if (flowMode == 1) Serial.println(F("1 (HWACK)"));
        else                    Serial.println(F("2 (SWFLOW)"));
        sendOK();
        return;
    }
    if (strncmp(uc, "ATSFLOW=", 8) == 0) {
        int val = atoi(uc + 8);
        if (val >= 0 && val <= 2) {
            flowMode = (uint8_t)val;
            // Clear SWFLOW window when switching modes.
            saveConfig();
            applyRadioConfig();
            sendOK();
        } else {
            sendError();
        }
        return;
    }

    // ── AT&F — factory reset ────────────────────────────────────────────────
    if (strcmp(uc, "AT&F") == 0) {
        factoryReset();
        Serial.print(F("\r\nFactory reset complete.\r\n"));
        sendOK();
        return;
    }

    // ── ATTEST-TX / ATTEST-RX / ATTEST-ECHO ─────────────────────────────────
    if (strcmp(uc, "ATTEST-TX") == 0)   { speedTestTX();   return; }
    if (strcmp(uc, "ATTEST-RX") == 0)   { speedTestRX();   return; }
    if (strcmp(uc, "ATTEST-ECHO") == 0) { speedTestEcho(); return; }

    // ── ATPING ──────────────────────────────────────────────────────────────
    // Only available from S_IDLE — no active or pending connection.
    if (strncmp(uc, "ATPING", 6) == 0 && strlen(uc) == 12) {
        if (state != S_IDLE) {
            Serial.print(F("\r\nERROR: ATPING only available when idle\r\n"));
            return;
        }
        uint8_t mac[3];
        if (!parseMac(uc + 6, mac)) { sendError(); return; }
        atPing(mac);
        return;
    }

    // ── ATREBOOT ────────────────────────────────────────────────────────────
    if (strcmp(uc, "ATREBOOT") == 0) {
        sendOK();
        Serial.flush();
        delay(100);
        wdt_enable(WDTO_15MS);
        while (true) {}
    }

    // ── AT (bare) ───────────────────────────────────────────────────────────
    if (strcmp(uc, "AT") == 0) { sendOK(); return; }

    sendError();
}

// ── Escape sequence ("+++ " with 1 s guard) ──────────────────────────────────
// Classic Hayes escape: 1 s silence, then +++, then 1 s silence.
// We track the last time any data was sent to detect the pre-guard.

void checkEscape(uint8_t b) {
    // Only meaningful in DATA mode
    if (state != S_DATA) { plusCount = 0; return; }

    unsigned long now = millis();

    // Pre-guard: 1 s since last data character
    if (b == '+') {
        if (plusCount == 0) {
            // Require 1 s of silence before first '+'
            if (now - lastDataMs >= 1000) {
                plusCount = 1;
                escapeArmed = true;
            }
        } else if (escapeArmed) {
            plusCount++;
            if (plusCount >= 3) {
                // Post-guard: wait 1 s after third '+' handled in loop
                plusCount = 255;  // sentinel
            }
        }
    } else {
        // Any non-'+' resets
        plusCount = 0;
        escapeArmed = false;
        lastDataMs = now;
    }
}

// ── Runtime radio health check ───────────────────────────────────────────────
// Called every HEALTH_INTERVAL_MS from loop(). Uses isChipConnected() which
// reads the CONFIG register over SPI — returns false if the module is absent
// or has lost power. On fault: drop any active connection, assert ER steady,
// clear MR. Recovery requires a hardware reset (power-cycle the Arduino).
void checkRadioHealth() {
    if (radioFailed) {
        // Already failed — keep ER lit and MR dark, nothing else to do.
        digitalWrite(LED_ER, HIGH);
        digitalWrite(LED_MR, LOW);
        return;
    }

    if (!radio.isChipConnected()) {
        radioFailed = true;
        cliPrintln(F("ERROR: nRF24L01 disconnected."));

        // Drop any active connection gracefully (best-effort; radio may be gone)
        if (state != S_IDLE) {
            state = S_IDLE;
            sendNoCarrier();   // prints NO CARRIER and flashes ER (already failing)
        }

        // Assert fault LEDs immediately — updateSteadyLEDs will maintain them.
        digitalWrite(LED_MR, LOW);
        digitalWrite(LED_ER, HIGH);
        ledErOff = 0;   // cancel any pending timer — ER stays on permanently
    }
}

// ── Transparent mode TX flush ────────────────────────────────────────────────
// Called every loop iteration when flowMode==0 and state==S_DATA.
// Sends accumulated bytes as a raw 32-byte payload (no header, no framing).
// Flushes when: buffer full, or S16 ms have passed since last byte (regS16, default 5).
void flushTransparent() {
    if (transBufLen == 0) return;
    bool full    = (transBufLen >= PAYLOAD_SIZE);
    bool timeout = (millis() - transLastByteMs >= (unsigned long)regS16);
    if (!full && !timeout) return;

    // Pad remainder with 0x00 so the remote knows payload length implicitly
    // from the content — it's the application's responsibility in this mode.
    // Send exactly PAYLOAD_SIZE bytes (nRF24L01 fixed payload).
    openWritePipe(remoteMac);
    radio.write(transBuf, PAYLOAD_SIZE);
    ledFlashSD();
    openListenPipes();
    transBufLen     = 0;
    transLastByteMs = 0;
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    ledSetup();
    loadConfig();   // read baudIdx before opening serial
    Serial.begin(baudTable[baudIdx]);

    if (!radio.begin()) {
        Serial.println(F("nRF24L01 not found. Check wiring."));
        radioFailed = true;
        digitalWrite(LED_MR, LOW);
        // Blink ER forever — stays in this loop so the user can see the fault
        // even without a serial terminal attached.
        while (true) {
            digitalWrite(LED_ER, HIGH);
            delay(200);
            digitalWrite(LED_ER, LOW);
            delay(200);
        }
    }

    applyRadioConfig();
    openListenPipes();

    if (!regS18) {
        Serial.print(F("\r\nnRF24L01 AT Modem "));
        Serial.print(F(MODEM_VERSION));
        Serial.println(F(" ready."));
        Serial.println(F("Type ATI for status."));
    }

    if (startupSlot < DIAL_SLOTS && dialStr[startupSlot][0] != '\0') {
        autoDial   = true;
        autoDialMs = millis() + 2000;
        Serial.print(F("\r\nAutodial: slot "));
        Serial.print(startupSlot);
        Serial.print(F(" -> "));
        Serial.println(dialStr[startupSlot]);
    }
}

// ── Main loop ─────────────────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();

    // ── 1. Autodial on startup ──────────────────────────────────────────────
    if (autoDial && now >= autoDialMs) {
        autoDial = false;
        if (state == S_IDLE) {
            char fakeCmd[DIAL_STR_LEN + 5];
            snprintf(fakeCmd, sizeof(fakeCmd), "ATD %s", dialStr[startupSlot]);
            cliPrint(F("Autodialling: "));
            if (inCliMode()) Serial.println(dialStr[startupSlot]);
            processCommand(fakeCmd);
        }
    }

    // ── 2. Read from serial ──────────────────────────────────────────────────
    while (Serial.available()) {
        uint8_t b = (uint8_t)Serial.read();
        lastSerialMs = millis();

        // XON/XOFF from host
        // Only intercept XON/XOFF bytes from host when S13=1.
        // With S13=0 they pass through as normal data.
        if (regS13 == 1) {
            if (b == 0x11) { continue; }              // XON — resume (already running)
            if (b == 0x13) { radioXoffRecv = true; continue; } // XOFF — pause TX
        }

        if (state == S_DATA) {
            if (echoOn) Serial.write(b);
            checkEscape(b);

            if (plusCount == 255) {
                // Third '+' received — start post-guard timer; don't push to TX
            } else if (!escapeArmed) {
                if (flowMode == 0) {
                    // Transparent mode: feed raw TX buffer directly
                    if (transBufLen < PAYLOAD_SIZE) {
                        transBuf[transBufLen++] = b;
                        transLastByteMs = millis();
                    }
                    // If buffer full, flush immediately via flushTransparent()
                    // called later in loop()
                } else {
                    txPush(b);
                }
            }
        } else {
            // Command mode
            if (echoOn) Serial.write(b);
            if (b == '\r' || b == '\n') {
                if (cmdLen > 0) {
                    cmdBuf[cmdLen] = '\0';
                    processCommand(cmdBuf);
                    cmdLen = 0;
                }
            } else if (b == 0x08 || b == 0x7F) {
                // Backspace
                if (cmdLen > 0) cmdLen--;
            } else if (cmdLen < CMD_BUF_SIZE - 1) {
                cmdBuf[cmdLen++] = (char)b;
            }
        }
    }

    // ── 3. Escape post-guard ─────────────────────────────────────────────────
    if (plusCount == 255 && (now - lastDataMs) >= 1000) {
        // Confirmed escape
        state = S_CONNECTED;   // stay connected but enter command mode
        plusCount = 0;
        escapeArmed = false;
        Serial.println();
        sendOK();
    }

    // ── 4. Connect timeout (S7) and dial retry (S8/S9) ─────────────────────
    static unsigned long connectStart = 0;
    if (state == S_CONNECTING) {
        if (connectStart == 0) connectStart = now;
        unsigned long timeoutMs = (unsigned long)regS7 * 1000UL;
        if (now - connectStart > timeoutMs) {
            // Carrier not received within S7 seconds.
            state = S_IDLE;
            connectStart = 0;
            // S8=255 = retry forever
            bool retryForever = (regS8 == 255);
            if (regS8 > 0 && (retryForever || dialRetryCount < regS8)
                && lastDialStr[0] != '\0') {
                dialRetryCount++;
                dialRetrying = true;
                dialRetryAt  = now + (unsigned long)regS9 * 1000UL;
                cliPrint(F("NO CARRIER - retry "));
                cliPrint(dialRetryCount);
                if (retryForever) cliPrintln(F("/forever"));
                else { cliPrint(F("/")); cliPrintln(regS8); }
                ledFlashER();
            } else {
                // Exhausted retries (or S8=0).
                dialRetryCount = 0;
                dialRetrying   = false;
                sendNoCarrier();
            }
        }
    } else {
        if (state != S_DATA && state != S_CONNECTED && state != S_RINGING) {
            connectStart = 0;
        }
    }

    // Fire a pending retry when S9 interval expires.
    if (dialRetrying && now >= dialRetryAt) {
        dialRetrying = false;
        cliPrint(F("Redialling (attempt "));
        cliPrint(dialRetryCount);
        if (regS8 == 255) cliPrintln(F("/forever)..."));
        else { cliPrint(F("/")); cliPrint(regS8); cliPrintln(F(")...")); }
        processCommand(lastDialStr);
    }

    // ── 5. Receive radio packets ─────────────────────────────────────────────
    // Drain pending packet from flushTxBuffer RX wait (avoids re-entrancy).
    if (pendingPktReady) {
        pendingPktReady = false;
        if (flowMode == 0 && (state == S_DATA || state == S_CONNECTED)) {
            ledFlashRD();
            for (uint8_t i = 0; i < PAYLOAD_SIZE; i++) {
                if (regS13 == 1 && (pendingPkt[i] == 0x11 || pendingPkt[i] == 0x13)) continue;
                if (!rxPush(pendingPkt[i])) { rxDropped++; ledFlashER(); }
            }
            checkFlowControl();
        } else {
            handleRadioPacket(pendingPkt);
        }
    }
    if (radio.available()) {
        uint8_t pkt[PAYLOAD_SIZE];
        radio.read(pkt, PAYLOAD_SIZE);
        if (flowMode == 0 && (state == S_DATA || state == S_CONNECTED)) {
            // Transparent mode: dump all bytes straight to serial.
            // The application owns framing/length — we write all PAYLOAD_SIZE bytes.
            ledFlashRD();
            for (uint8_t i = 0; i < PAYLOAD_SIZE; i++) {
                if (regS13 == 1 && (pkt[i] == 0x11 || pkt[i] == 0x13)) continue; // honour XON/XOFF filter
                if (!rxPush(pkt[i])) { rxDropped++; ledFlashER(); }
            }
            checkFlowControl();
        } else {
            handleRadioPacket(pkt);
        }
    }

    // ── 6. Drain RX buffer → serial ─────────────────────────────────────────
    while (rxAvail() > 0) {
        int b = rxPop();
        if (b < 0) break;
        Serial.write((uint8_t)b);
    }

    // ── 7. Flush TX buffer → radio ───────────────────────────────────────────
    if (flowMode == 0 && (state == S_DATA || state == S_CONNECTED)) {
        flushTransparent();
    } else {
        flushTxBuffer();
    }

    // ── 8. Flow control check ────────────────────────────────────────────────
    checkFlowControl();

    // ── 9. Ring timeout ──────────────────────────────────────────────────────
    // RING is now printed directly in the PKT_CONN handler, once per packet.
    // This block only handles timeout: if no PKT_CONN arrives within the
    // caller's full retry window, silently return to IDLE.
    if (state == S_RINGING && lastConnMs != 0) {
        unsigned long ringTimeoutMs;
        if (regS8 == 255) ringTimeoutMs = 3600000UL;  // 1 hour ~= forever
        else ringTimeoutMs =
            (unsigned long)(regS8 + 1) * (unsigned long)regS7 * 1000UL
            + (unsigned long)regS8     * (unsigned long)regS9 * 1000UL
            + 2000UL;
        if (millis() - lastConnMs > ringTimeoutMs) {
            state      = S_IDLE;
            ringCount  = 0;
            lastConnMs = 0;
            openListenPipes();
            cliPrintln(F("NO ANSWER"));
        }
    }

    // ── 10. Keep-alive tick ──────────────────────────────────────────────────
    if (regS10 && kaInitiator && (state == S_DATA || state == S_CONNECTED)) {
        if (kaPingAt == 0)
            kaPingAt = now + (unsigned long)regS11 * 1000UL;

        if (now >= kaPingAt) {
            if (kaWaitingPong) {
                kaMissed++;
                cliPrint(F("KA miss "));
                cliPrint(kaMissed);
                cliPrint('/');
                cliPrintln(regS12);
                ledFlashER();
                if (kaMissed >= regS12) {
                    lastDisconnectMs = millis();
                    clearBuffers();
                    kaMissed      = 0;
                    kaWaitingPong = false;
                    kaPingAt      = 0;
                    kaLastPingMs  = 0;
                    state = S_IDLE;
                    openListenPipes();
                    sendNoCarrier();
                } else {
                    sendControlPacket(PKT_PING);
                    kaPingAt = now + (unsigned long)regS11 * 1000UL;
                }
            } else {
                sendControlPacket(PKT_PING);
                kaWaitingPong = true;
                kaPingAt      = now + (unsigned long)regS11 * 1000UL;
            }
        }
    } else if (!kaInitiator && (state == S_DATA || state == S_CONNECTED)) {
        // Answerer watchdog: we don't send pings, but we track receiving them.
        if (regS10 && kaLastPingMs != 0) {
            // Timeout = S11 * S12 seconds — the full window the initiator would
            // exhaust before giving up. If we haven't seen a ping in that window
            // the initiator is gone (or their S10 was turned off mid-session).
            // Use millis() directly here (not stale 'now') because kaLastPingMs
            // may have been set mid-loop by doAnswer(), which would cause
            // unsigned subtraction underflow with the pre-captured 'now'.
            unsigned long windowMs = (unsigned long)regS11 * (unsigned long)regS12 * 1000UL;
            if (millis() - kaLastPingMs > windowMs) {
                cliPrintln(F("KA timeout - no pings received"));
                ledFlashER();
                lastDisconnectMs = millis();
                clearBuffers();
                kaInitiator  = false;
                kaMissed     = 0;
                kaLastPingMs = 0;
                state = S_IDLE;
                openListenPipes();
                sendNoCarrier();
            }
        } else if (!regS10) {
            // S10=0 on answerer: reply to pings but never disconnect due to silence.
            kaLastPingMs = 0;
        }
    }



    // ── 13. Radio health check (every 500 ms) ────────────────────────────────
    if (now - lastHealthMs >= HEALTH_INTERVAL_MS) {
        lastHealthMs = now;
        checkRadioHealth();
    }

    // ── 14. LED updates ──────────────────────────────────────────────────────
    updateSteadyLEDs();
    updateFlashLEDs();
}

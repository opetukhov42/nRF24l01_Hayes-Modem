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
 * AT commands:
 *   ATI              – print modem info (model, channel, own MAC, remote MAC, RSSI)
 *   ATSPECTRUM        – sweep all 126 channels, print ASCII spectrum, any key stops
 *   ATSMYMAC=XXYYZZ  – set own 3-byte MAC (hex, e.g. ATSMYMAC=A1B2C3)
 *   ATSMYMAC?        – query own MAC
 *   ATSETCH=nn       – set RF channel 0-125 (e.g. ATSETCH=76)
 *   ATSETCH?         – query RF channel
 *   ATSSPEED=n       – set link speed: 0=250k, 1=1M, 2=2M
 *   ATSSPEED?        – query link speed
 *   ATD XXYYZZ       – dial / connect to remote MAC (returns BUSY if channel occupied and S14=1)
 *   ATA              – manually answer an incoming call
 *   ATH              – hang up / reject incoming call
 *   ATSn=value       – set S-register n to value (all saved to EEPROM):
 *                      S0  auto-answer rings (0=off)
 *                      S6  pre-dial wait seconds (default 0, radio needs no dialtone)
 *                      S7  carrier wait / connect timeout seconds (default 3)
 *                      S8  dial retry attempts after failure (default 3, 0=no retry)
 *                      S9  inter-retry interval seconds (default 3)
 *                      S10 keep-alive enable (1=on default, 0=off)
 *                      S11 keep-alive interval seconds (default 5)
 *                      S12 missed pongs before drop (default 3)
 *                      S13 flow control: 0=none, 1=XON/XOFF (default 1)
 *                      S14 busy detect enable: 1=on (default), 0=off
 *                      S15 channel scan duration ms before dial (default 50)
 *                      S16 transparent mode TX idle flush ms (default 5)
 *                      S17 spectrum scan dwell ms per channel (default 20)
 *   ATSn?            – query S-register n (returns zero-padded 3-digit value)
 *   AT&F            – factory reset: restore all defaults and overwrite EEPROM
 *   AT&Zn=string     – store dial string in slot n (n=0-3); AT&Zn= clears slot
 *   AT&Zn?           – query stored dial string in slot n
 *   AT&Yn            – set startup autodial slot (fires 2 s after boot)
 *   AT&Y?            – query startup autodial slot
 *   ATSBAUD=n        – set baud rate (9600/19200/38400/57600/115200/250000/500000/1000000)
 *                    OK is sent at old rate, then port switches — match your terminal!
 *   ATSBAUD?         – query current baud rate
 *   ATSFLOW=n        – set flow/ACK mode: 0=transparent (no framing/ACK), 1=HW ACK, 2=SW flow control (default)
 *   ATSFLOW?         – query flow mode
 *   ATE0 / ATE1      – echo off / on
 *   +++              – escape data mode → command mode (1 s guard time each side)
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
 *   [1]  seq    – rolling 0-255 sequence number
 *   [2]  length – number of payload bytes that follow (0-29)
 *   [3..31] payload
 */

#include <SPI.h>
#include <RF24.h>
#include <EEPROM.h>

// ── Firmware version ───────────────────────────────────────────────────────────
// Increment minor version (v1.x.0) on every code modification.
#define MODEM_VERSION "v1.25.0"

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
uint8_t  dialRetryCount  = 0;          // retries fired so far for current dial
bool     dialRetrying    = false;      // true while waiting between retries
unsigned long dialRetryAt = 0;         // millis() when next retry fires
char     lastDialStr[DIAL_STR_LEN + 5]; // "ATD XXYYZZ" copy for retransmission

// Incoming call state (used while S_RINGING)
uint8_t  pendingMac[3]  = {0, 0, 0};  // MAC of the caller waiting to be answered
uint8_t  ringCount   = 0;   // how many RINGs sent (for S0 auto-answer threshold)
unsigned long lastConnMs = 0;  // millis() of last PKT_CONN received (ring timeout)

// Circular buffers
uint8_t rxBuf[RX_BUF_SIZE];
volatile uint16_t rxHead = 0, rxTail = 0;

uint8_t txBuf[TX_BUF_SIZE];
uint16_t txHead = 0, txTail = 0;

// Command line accumulator
char cmdBuf[CMD_BUF_SIZE];
uint8_t cmdLen = 0;

// XON/XOFF state
bool hostXoffSent  = false;   // we told the host to stop
bool radioXoffRecv  = false;  // remote told us to stop
bool radioXoffSent  = false;  // we told remote to stop
bool yieldToRemote  = false;  // remote sent SWACK_YIELD — pause our TX, let them send

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
uint8_t dataTxSeq = 0;    // sequence counter for PKT_DATA only (SWFLOW window)
uint8_t rxExpected = 0;

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
// Valid indices 0-7 matching baudTable[]. Default index 4 = 115200.
const uint32_t baudTable[] = {
    9600UL, 19200UL, 38400UL, 57600UL,
    115200UL, 250000UL, 500000UL, 1000000UL
};
const uint8_t  BAUD_TABLE_SIZE = 8;
const uint8_t  BAUD_DEFAULT    = 4;   // 115200
uint8_t        baudIdx         = BAUD_DEFAULT;
bool           pendingBaudChange = false;   // apply after OK is flushed

// Dial string profiles and startup autodial
char    dialStr[DIAL_SLOTS][DIAL_STR_LEN + 1];  // "" = empty slot
uint8_t startupSlot = 0xFF;                      // 0xFF = autodial disabled
bool    autoDial    = false;   // armed in setup() if startup slot is valid + non-empty
unsigned long autoDialMs = 0;  // millis() timestamp to fire autodial

// ── SWFLOW retransmit window ──────────────────────────────────────────────────
// Holds up to SW_WIN_SIZE unacknowledged outbound packets so we can retransmit
// on PKT_NACK. Each slot stores the raw 32-byte payload and a send timestamp.
#define SW_WIN_SIZE   4
#define SW_RETX_MS  2000  // retransmit slot if no SWACK within 2 s (safety net only;
                               //   primary recovery is NACK-based)
#define SW_ACK_WAIT_MS  5  // ms to stay in RX after sending a data packet,
                               //   giving the remote time to send its SWACK back

struct SwSlot {
    uint8_t  pkt[PAYLOAD_SIZE];
    uint32_t sentMs;
    bool     used;
};
SwSlot   swWin[SW_WIN_SIZE];
uint8_t  swWinHead = 0;           // next slot to try (0-3, always kept in range)
uint8_t  swAckedSeq = 0;    // TX side: last dataTxSeq the remote confirmed via SWACK
bool     swAckValid = false;    // TX side: have we received any SWACK yet?
uint8_t  rxAckedSeq = 0;    // RX side: last incoming data seq we ACKed
bool     rxAckValid = false;    // RX side: have we received any PKT_DATA yet?

// Radio health
bool     radioFailed   = false;  // set on init fail or runtime disconnect
unsigned long lastHealthMs = 0;  // last time we ran isChipConnected()
#define HEALTH_INTERVAL_MS  500  // check every 500 ms

// Escape sequence detection
unsigned long lastDataMs = 0;
uint8_t plusCount = 0;
bool escapeArmed = false;

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
    addr[2] = 0xEF;
    addr[3] = mac[0];
    addr[4] = mac[1] ^ mac[2];   // fold last byte for variety
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
    regS8 = EEPROM.read(EE_S8); if (regS8 == 0xFF) regS8 = 3;
    regS9  = EEPROM.read(EE_S9);  if (regS9  == 0xFF) regS9  = 3;
    regS10 = EEPROM.read(EE_S10); if (regS10 == 0xFF) regS10 = 1;
    regS11 = EEPROM.read(EE_S11); if (regS11 == 0xFF) regS11 = 5;
    regS12 = EEPROM.read(EE_S12); if (regS12 == 0xFF) regS12 = 3;
    regS13 = EEPROM.read(EE_S13); if (regS13 > 1)       regS13 = 1;
    regS14 = EEPROM.read(EE_S14); if (regS14 > 1)       regS14 = 1;
    regS15 = EEPROM.read(EE_S15); if (regS15 == 0 || regS15 == 0xFF) regS15 = 50;
    regS16 = EEPROM.read(EE_S16); if (regS16 == 0 || regS16 == 0xFF) regS16 = 5;
    regS17 = EEPROM.read(EE_S17); if (regS17 == 0 || regS17 == 0xFF) regS17 = 20;
}

void saveConfig() {
    EEPROM.write(EE_OWN_MAC,     ownMac[0]);
    EEPROM.write(EE_OWN_MAC + 1, ownMac[1]);
    EEPROM.write(EE_OWN_MAC + 2, ownMac[2]);
    EEPROM.write(EE_REM_MAC,     remoteMac[0]);
    EEPROM.write(EE_REM_MAC + 1, remoteMac[1]);
    EEPROM.write(EE_REM_MAC + 2, remoteMac[2]);
    EEPROM.write(EE_CHANNEL,  channel);
    EEPROM.write(EE_SPEED,    speedEnum);
    EEPROM.write(EE_S0,       regS0);
    EEPROM.write(EE_FLOW,     flowMode);
    EEPROM.write(EE_BAUD,     baudIdx);
    for (uint8_t s = 0; s < DIAL_SLOTS; s++) {
        uint8_t base = EE_DIALSTR0 + s * (DIAL_STR_LEN + 1);
        for (uint8_t c = 0; c <= DIAL_STR_LEN; c++)
            EEPROM.write(base + c, (uint8_t)dialStr[s][c]);
    }
    EEPROM.write(EE_STARTUP,  startupSlot);
    EEPROM.write(EE_S6,       regS6);
    EEPROM.write(EE_S7,       regS7);
    EEPROM.write(EE_S8,       regS8);
    EEPROM.write(EE_S9,       regS9);
    EEPROM.write(EE_S10,      regS10);
    EEPROM.write(EE_S11,      regS11);
    EEPROM.write(EE_S12,      regS12);
    EEPROM.write(EE_S13,      regS13);
    EEPROM.write(EE_S14,      regS14);
    EEPROM.write(EE_S15,      regS15);
    EEPROM.write(EE_S16,      regS16);
    EEPROM.write(EE_S17,      regS17);
    EEPROM.write(EE_MAGIC,    EEPROM_MAGIC);
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
    bool wasListening = radio.isChipConnected();
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
void sendOK()        { Serial.print(F("\r\nOK\r\n")); }
void sendError()     { Serial.print(F("\r\nERROR\r\n")); }
void sendNoCarrier() { Serial.print(F("\r\nNO CARRIER\r\n")); ledFlashER(); }
void sendConnect()   { Serial.print(F("\r\nCONNECT\r\n")); }
void sendRing()      { Serial.print(F("\r\nRING\r\n")); }

// Print diagnostic/unsolicited text only when in CLI mode (not raw data mode).
// In S_DATA the serial stream is raw — injecting text corrupts it.
// S_CONNECTED = data mode but user has escaped via +++ so CLI is restored.
inline bool inCliMode() { return state != S_DATA; }
void cliPrint(const __FlashStringHelper *s)   { if (inCliMode()) Serial.print(s); }
void cliPrintln(const __FlashStringHelper *s) { if (inCliMode()) Serial.println(s); }
void cliPrint(uint8_t v)                      { if (inCliMode()) Serial.print(v); }
void cliPrintln(uint8_t v)                    { if (inCliMode()) Serial.println(v); }
void cliPrint(char c)                         { if (inCliMode()) Serial.print(c); }


// ── Packet transmit ───────────────────────────────────────────────────────────
bool sendPacket(uint8_t type, const uint8_t *data, uint8_t len) {
    uint8_t pkt[PAYLOAD_SIZE];
    memset(pkt, 0, PAYLOAD_SIZE);
    pkt[0] = type;
    pkt[1] = txSeq++;
    pkt[2] = (len > MAX_DATA) ? MAX_DATA : len;
    if (data && pkt[2] > 0)
        memcpy(pkt + DATA_OFFSET, data, pkt[2]);

    // In SWFLOW mode, store DATA packets in the retransmit window.
    // dataTxSeq is a data-only counter so window seq numbers are not
    // polluted by interleaved control packets (PING, PONG, SWACK etc.).
    if (flowMode == 2 && type == PKT_DATA) {
        pkt[1] = dataTxSeq;          // overwrite with data-only seq
        // Find the next free window slot (linear scan, SW_WIN_SIZE=4 so cheap).
        // This is safer than modulo-indexing which can overwrite unACKed packets
        // when swWinHead wraps around on long sessions.
        uint8_t slotIdx = 0xFF;
        for (uint8_t i = 0; i < SW_WIN_SIZE; i++) {
            uint8_t candidate = (swWinHead + i) % SW_WIN_SIZE;
            if (!swWin[candidate].used) { slotIdx = candidate; break; }
        }
        if (slotIdx == 0xFF) {
            // All slots occupied — this shouldn't happen because flushTxBuffer
            // checks inFlight < SW_WIN_SIZE before calling sendPacket, but
            // guard here anyway to prevent corruption.
            openListenPipes();
            return false;
        }
        memcpy(swWin[slotIdx].pkt, pkt, PAYLOAD_SIZE);
        swWin[slotIdx].sentMs = millis();
        swWin[slotIdx].used   = true;
        dataTxSeq++;
        swWinHead = (swWinHead + 1) % SW_WIN_SIZE;  // keep in 0-3 range
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
void flushTxBuffer() {
    if (state != S_DATA && state != S_CONNECTED) return;
    if (radioXoffRecv) return;   // remote asked us to pause

    // Cooperative duplex (SWFLOW only): remote sent SWACK_YIELD meaning it has
    // data queued for us. Skip this TX turn so it can transmit without collision.
    // Flag cleared here — remote gets exactly one uncontested TX slot per yield,
    // then we resume normally next loop iteration.
    // In HWACK/none mode yieldToRemote is never set so this path is never taken.
    if (yieldToRemote) {
        yieldToRemote = false;
        return;
    }

    while (txAvail() > 0) {
        // In SWFLOW mode, stop sending new packets when the window is full.
        if (flowMode == 2) {
            uint8_t inFlight = 0;
            for (uint8_t i = 0; i < SW_WIN_SIZE; i++)
                if (swWin[i].used) inFlight++;
            if (inFlight >= SW_WIN_SIZE) break;
        }

        uint8_t chunk[MAX_DATA];
        uint8_t n = 0;
        while (n < MAX_DATA && txAvail() > 0) {
            int b = txPop();
            if (b < 0) break;
            chunk[n++] = (uint8_t)b;
        }
        if (n == 0) break;
        if (!sendPacket(PKT_DATA, chunk, n)) {
            break;
        }
        // Brief RX window after each data packet so the remote's SWACK can
        // arrive before we TX again. Process SWACK/NACK immediately (safe —
        // they only update window state, no re-entrant sendPacket calls).
        // Defer PKT_DATA to pendingPkt so swflowAckData() runs in main loop
        // context, not re-entrantly inside our TX loop.
        if (flowMode == 2) {
            unsigned long waitUntil = millis() + SW_ACK_WAIT_MS;
            while (millis() < waitUntil) {
                if (radio.available()) {
                    uint8_t tmpPkt[PAYLOAD_SIZE];
                    radio.read(tmpPkt, PAYLOAD_SIZE);
                    uint8_t ptype = tmpPkt[0];
                    if (ptype == PKT_SWACK || ptype == PKT_SWACK_YIELD ||
                        ptype == PKT_NACK  || ptype == PKT_XON ||
                        ptype == PKT_XOFF) {
                        // Safe to process immediately — no re-entrant TX
                        handleRadioPacket(tmpPkt);
                    } else {
                        // PKT_DATA or other — defer to main loop
                        if (!pendingPktReady) {
                            memcpy(pendingPkt, tmpPkt, PAYLOAD_SIZE);
                            pendingPktReady = true;
                        }
                    }
                    break;
                }
            }
        }
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

    // Tell remote to stop
    if (!radioXoffSent && used >= XOFF_THRESHOLD) {
        sendControlPacket(PKT_XOFF);
        radioXoffSent = true;
    }
    if (radioXoffSent && used <= XON_THRESHOLD) {
        sendControlPacket(PKT_XON);
        radioXoffSent = false;
    }
}

// ── Process an incoming radio packet ─────────────────────────────────────────
void handleRadioPacket(const uint8_t *pkt) {
    uint8_t type = pkt[0];
    // uint8_t seq = pkt[1];   // could be used for duplicate detection
    uint8_t len  = pkt[2];

    switch (type) {
        case PKT_DATA:
            if (state == S_DATA || state == S_CONNECTED) {
                ledFlashRD();
                swflowAckData(pkt[1]);   // no-op unless SWFLOW mode
                for (uint8_t i = 0; i < len && i < MAX_DATA; i++) {
                    rxPush(pkt[DATA_OFFSET + i]);
                }
                checkFlowControl();
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
                for (uint8_t i = 0; i < SW_WIN_SIZE; i++) swWin[i].used = false;
                swAckValid    = false;
                swWinHead     = 0;
                dataTxSeq     = 0;
                swAckedSeq    = 0;
                rxAckValid    = false;
                rxAckedSeq    = 0;
                kaInitiator   = false;
                kaMissed      = 0;
                kaWaitingPong = false;
                kaPingAt      = 0;
                kaLastPingMs  = 0;
                yieldToRemote = false;
                state = S_IDLE;
                openListenPipes();
                sendNoCarrier();
            }
            break;

        case PKT_XON:
            radioXoffRecv = false;
            // Reset retransmit timers so slots don't immediately fire
            // after the pause — they were paused, not lost.
            for (uint8_t i = 0; i < SW_WIN_SIZE; i++)
                if (swWin[i].used) swWin[i].sentMs = millis();
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
                kaInitiator   = true;
                kaMissed      = 0;
                kaWaitingPong = false;
                kaPingAt      = millis() + (unsigned long)regS11 * 1000UL;
                sendConnect();           // print while still in CLI mode
                state         = S_DATA;  // switch AFTER printing CONNECT
            }
            break;

        case PKT_NACK:
            // Remote detected a gap — retransmit the requested seq from our window.
            if (flowMode == 2 && len >= 1) {
                uint8_t wantSeq = pkt[DATA_OFFSET];
                for (uint8_t i = 0; i < SW_WIN_SIZE; i++) {
                    if (swWin[i].used && swWin[i].pkt[1] == wantSeq) {
                        openWritePipe(remoteMac);
                        bool ok = radio.write(swWin[i].pkt, PAYLOAD_SIZE);
                        if (ok) ledFlashSD(); else ledFlashER();
                        openListenPipes();
                        swWin[i].sentMs = millis();   // reset retx timer
                        break;
                    }
                }
            }
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

        case PKT_SWACK_YIELD:
            // Remote ACKs our data AND requests we yield TX — it has data to send.
            yieldToRemote = true;
            // Fall through to normal SWACK window-advance logic.
            /* fall through */
        case PKT_SWACK:
            // Cumulative ACK: remote has received everything up to and including seq N.
            if (flowMode == 2 && len >= 1) {
                uint8_t ackedSeq = pkt[DATA_OFFSET];
                swAckedSeq  = ackedSeq;
                swAckValid  = true;
                // Free all window slots with seq <= ackedSeq (handles wraparound).
                for (uint8_t i = 0; i < SW_WIN_SIZE; i++) {
                    if (swWin[i].used) {
                        uint8_t s = swWin[i].pkt[1];
                        // Wraparound-safe: distance from ackedSeq back to s.
                        uint8_t age = (uint8_t)(ackedSeq - s + 256) % 256;
                        if (age < 128) swWin[i].used = false;  // s <= ackedSeq
                    }
                }
            }
            break;
    }
}

// ── SWFLOW receiver: send SWACK and detect gaps ───────────────────────────────
// Called from handleRadioPacket for every PKT_DATA in SWFLOW mode.
// Sends a NACK if a gap is detected, then a SWACK after each in-order packet.
void swflowAckData(uint8_t seq) {
    if (flowMode != 2) return;   // SWACK only used in SWFLOW mode

    if (rxAckValid) {
        uint8_t expected = (uint8_t)(rxAckedSeq + 1);
        if (seq != expected) {
            // Gap detected — request the missing seq.
            uint8_t nackPkt[PAYLOAD_SIZE];
            memset(nackPkt, 0, PAYLOAD_SIZE);
            nackPkt[0] = PKT_NACK;
            nackPkt[1] = txSeq++;
            nackPkt[2] = 1;
            nackPkt[DATA_OFFSET] = expected;
            openWritePipe(remoteMac);
            radio.write(nackPkt, PAYLOAD_SIZE);
            openListenPipes();
            return;   // don't advance rxAckedSeq until gap is filled
        }
    }
    // In-order: send SWACK. If we also have data pending, use PKT_SWACK_YIELD
    // to ask the remote to pause its TX — cooperative half-duplex token passing.
    rxAckedSeq = seq;
    rxAckValid = true;
    uint8_t ackPkt[PAYLOAD_SIZE];
    memset(ackPkt, 0, PAYLOAD_SIZE);
    bool weHaveData = (txAvail() > 0) && !radioXoffRecv;
    ackPkt[0] = weHaveData ? PKT_SWACK_YIELD : PKT_SWACK;
    ackPkt[1] = txSeq++;
    ackPkt[2] = 1;
    ackPkt[DATA_OFFSET] = seq;
    openWritePipe(remoteMac);
    bool ok = radio.write(ackPkt, PAYLOAD_SIZE);
    if (ok) ledFlashSD(); else ledFlashER();
    openListenPipes();
}

// Complete an incoming call: copy pending MAC, ACK the caller, enter DATA mode.
void doAnswer() {
    memcpy(remoteMac, pendingMac, 3);
    saveConfig();                  // persist new remote MAC
    clearBuffers();                // discard any stale pre-connect data
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
    clearBuffers();
    dataTxSeq      = 0;
    swWinHead      = 0;
    swAckedSeq     = 0;
    swAckValid     = false;
    rxAckedSeq     = 0;
    rxAckValid     = false;

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

    while (true) {
        // Check for keypress — any byte stops the scan
        if (Serial.available()) {
            Serial.read();
            break;
        }

        Serial.print(F("     "));   // indent to align with ruler

        for (uint8_t ch = 0; ch < 126; ch++) {
            // Switch channel and start listening — this is essential:
            // RPD only works in RX mode, and startListening() resets the
            // RPD latch so stale detections from the previous channel don't
            // carry over. Without this, testRPD() always returns 0.
            radio.setChannel(ch);
            radio.startListening();
            // Wait at least 170 µs for the synthesiser to settle and the
            // RPD comparator to become valid after startListening().
            delayMicroseconds(200);

            // Sample RPD for regS17 milliseconds.
            // The RPD latch is set by hardware when signal > -64 dBm and
            // cleared only by startListening() — so a single hit per
            // channel is meaningful. We count hits across multiple 1ms
            // samples to get a density measure.
            uint8_t hits = 0;
            unsigned long start = millis();
            while (millis() - start < (unsigned long)regS17) {
                if (radio.testRPD()) hits++;
                // Re-arm the latch: stopListening()+startListening() resets RPD
                // so we can detect multiple bursts within the dwell window.
                radio.stopListening();
                radio.startListening();
                delayMicroseconds(200);
            }

            // Map hits to density character (clamp to index 9)
            uint8_t idx = hits / 2;
            if (idx > 9) idx = 9;
            Serial.write(density[idx]);

            // Check for keypress between channels
            if (Serial.available()) {
                Serial.read();
                goto scan_done;
            }
        }
        Serial.println();   // end of sweep — CR+LF
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
        // startListening() resets the RPD latch — each call is a fresh reading.
        radio.startListening();
        delayMicroseconds(1800);     // datasheet: ~170 µs min; 1800 µs is safe
        if (radio.testRPD()) hits++;
    }

    openListenPipes();

    // Require at least 2 hits to declare busy — prevents single noise spikes
    // from blocking a dial attempt on what is actually a clear channel.
    return (hits >= 2);
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
    mac[0] = (uint8_t)strtol(tmp,     nullptr, 16) >> 4 & 0xFF;
    // Manual 2-digit hex parse to avoid strtol offset issues:
    char h[3] = {0};
    h[0] = tmp[0]; h[1] = tmp[1]; mac[0] = (uint8_t)strtol(h, nullptr, 16);
    h[0] = tmp[2]; h[1] = tmp[3]; mac[1] = (uint8_t)strtol(h, nullptr, 16);
    h[0] = tmp[4]; h[1] = tmp[5]; mac[2] = (uint8_t)strtol(h, nullptr, 16);
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
    char uc[CMD_BUF_SIZE];
    uint8_t i = 0;
    while (cmd[i] && i < CMD_BUF_SIZE - 1) {
        uc[i] = toupper((unsigned char)cmd[i]);
        i++;
    }
    uc[i] = '\0';

    // ── ATI ─────────────────────────────────────────────────────────────────
    if (strcmp(uc, "ATI") == 0) {
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
        Serial.print(F("S8      : ")); Serial.print(regS8); Serial.println(F("    (retry attempts)"));
        Serial.print(F("S9      : ")); Serial.print(regS9);  Serial.println(F(" s  (retry interval)"));
        Serial.print(F("S10     : ")); Serial.print(regS10); Serial.println(regS10 ? F("    (keep-alive ON)") : F("    (keep-alive OFF)"));
        Serial.print(F("S11     : ")); Serial.print(regS11); Serial.println(F(" s  (keep-alive interval)"));
        Serial.print(F("S12     : ")); Serial.print(regS12); Serial.println(F("    (missed pongs before drop)"));
        Serial.print(F("S13     : ")); Serial.println(regS13 == 1 ? F("1 (XON/XOFF enabled)") : F("0 (no flow control)"));
        Serial.print(F("S14     : ")); Serial.println(regS14 == 1 ? F("1 (busy detect ON)") : F("0 (busy detect OFF)"));
        Serial.print(F("S15     : ")); Serial.print(regS15); Serial.println(F(" ms (channel scan duration)"));
        Serial.print(F("S16     : ")); Serial.print(regS16); Serial.println(F(" ms (transparent TX idle flush)"));
        Serial.print(F("S17     : ")); Serial.print(regS17); Serial.println(F(" ms (spectrum scan dwell per channel)"));
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

    // ── ATE0 / ATE1 ─────────────────────────────────────────────────────────
    if (strcmp(uc, "ATE0") == 0) { echoOn = false; sendOK(); return; }
    if (strcmp(uc, "ATE1") == 0) { echoOn = true;  sendOK(); return; }

    // ── ATH ─────────────────────────────────────────────────────────────────
    if (strcmp(uc, "ATH") == 0 || strcmp(uc, "ATH0") == 0) {
        lastDisconnectMs = millis();
        pendingPktReady  = false;
        // Clear SWFLOW window, buffers, and cancel any pending retry on hangup.
        clearBuffers();
        transBufLen = 0;
        for (uint8_t i = 0; i < SW_WIN_SIZE; i++) swWin[i].used = false;
        swAckValid     = false;
        swWinHead      = 0;
        dataTxSeq      = 0;
        swAckedSeq     = 0;
        rxAckValid     = false;
        rxAckedSeq     = 0;
        dialRetrying   = false;
        dialRetryCount = 0;
        kaInitiator    = false;
        kaMissed       = 0;
        kaWaitingPong  = false;
        kaPingAt       = 0;
        kaLastPingMs   = 0;
        yieldToRemote  = false;
        if (state == S_RINGING) {
            // Reject incoming call — send DISC back to caller using pending MAC.
            memcpy(remoteMac, pendingMac, 3);
            sendControlPacket(PKT_DISC);
            memset(remoteMac, 0, 3);
            ringCount  = 0;
            lastConnMs = 0;
            state = S_IDLE;
            openListenPipes();
        } else if (state == S_DATA || state == S_CONNECTED || state == S_CONNECTING) {
            sendControlPacket(PKT_DISC);
            state = S_IDLE;
            openListenPipes();
        }
        sendOK();
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

            // S6: pre-dial wait.
            uint8_t prewait = (regS6 > 10) ? 10 : regS6;
            if (prewait > 0) delay((uint16_t)prewait * 1000UL);

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
            for (uint8_t i = 0; i < SW_WIN_SIZE; i++) swWin[i].used = false;
            swAckValid = false;
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

    Serial.print(F("\r\nnRF24L01 AT Modem "));
    Serial.print(F(MODEM_VERSION));
    Serial.println(F(" ready."));
    Serial.println(F("Type ATI for status."));

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
            if (regS8 > 0 && dialRetryCount < regS8 && lastDialStr[0] != '\0') {
                // Schedule a retry after S9 seconds.
                dialRetryCount++;
                dialRetrying = true;
                dialRetryAt  = now + (unsigned long)regS9 * 1000UL;
                cliPrint(F("NO CARRIER - retry "));
                cliPrint(dialRetryCount);
                cliPrint(F("/"));
                cliPrintln(regS8);
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
        cliPrint(F("/"));
        cliPrint(regS8);
        cliPrintln(F(")..."));
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
        unsigned long ringTimeoutMs =
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
                    for (uint8_t i = 0; i < SW_WIN_SIZE; i++) swWin[i].used = false;
                    swAckValid    = false;
                    swWinHead     = 0;
                    dataTxSeq     = 0;
                    swAckedSeq    = 0;
                    rxAckValid    = false;
                    rxAckedSeq    = 0;
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
                for (uint8_t i = 0; i < SW_WIN_SIZE; i++) swWin[i].used = false;
                swAckValid   = false;
                swWinHead    = 0;
                dataTxSeq    = 0;
                swAckedSeq   = 0;
                rxAckValid   = false;
                rxAckedSeq   = 0;
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

        // ── 11. SWFLOW: retransmit timed-out window slots ────────────────────────
    if (flowMode == 2 && (state == S_DATA || state == S_CONNECTED)
        && !radioXoffRecv) {   // don't retransmit while remote says stop
        for (uint8_t i = 0; i < SW_WIN_SIZE; i++) {
            if (swWin[i].used && (millis() - swWin[i].sentMs) >= SW_RETX_MS) {
                openWritePipe(remoteMac);
                bool ok = radio.write(swWin[i].pkt, PAYLOAD_SIZE);
                if (ok) ledFlashSD(); else ledFlashER();
                openListenPipes();
                swWin[i].sentMs = millis();
            }
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

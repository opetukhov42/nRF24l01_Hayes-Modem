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
 *                      S7  carrier wait / connect timeout seconds (default 1, radio ACK is near-instant)
 *                      S8  dial retry attempts after failure (default 3, 0=no retry)
 *                      S9  inter-retry interval seconds (default 10)
 *                      S10 keep-alive enable (1=on default, 0=off)
 *                      S11 keep-alive interval seconds (default 5)
 *                      S12 missed pongs before drop (default 3)
 *                      S13 flow control: 0=none, 1=XON/XOFF (default 1)
 *                      S14 busy detect enable: 1=on (default), 0=off
 *                      S15 channel scan duration ms before dial (default 50)
 *   ATSn?            – query S-register n (returns zero-padded 3-digit value)
 *   AT&F            – factory reset: restore all defaults and overwrite EEPROM
 *   AT&Zn=string     – store dial string in slot n (n=0-3); AT&Zn= clears slot
 *   AT&Zn?           – query stored dial string in slot n
 *   AT&Yn            – set startup autodial slot (fires 2 s after boot)
 *   AT&Y?            – query startup autodial slot
 *   ATSBAUD=n        – set baud rate (9600/19200/38400/57600/115200/250000/500000/1000000)
 *                    OK is sent at old rate, then port switches — match your terminal!
 *   ATSBAUD?         – query current baud rate
 *   ATSHWACK=n       – set ACK mode: 1=hardware ACK (default), 0=software flow control
 *   ATSHWACK?        – query ACK mode
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
#define EE_HWACK     10    // byte 10    1=HWACK mode, 0=SWFLOW mode
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
uint8_t channel      = 76;
uint8_t speedEnum    = 1;   // 0=250k 1=1M 2=2M
bool    echoOn       = true;
uint8_t regS0        = 0;     // S0 register: 0=manual answer, 1+=auto-answer after n rings
uint8_t regS6        = 0;     // S6: seconds to wait before first dial attempt (0=none, radio needs no dialtone)
uint8_t regS7        = 1;     // S7: seconds to wait for carrier (1 s, radio ACK is near-instant)
uint8_t regS8        = 3;     // S8: number of retry attempts after first failure
uint8_t regS9        = 10;    // S9: seconds between retry attempts
uint8_t regS10       = 1;     // S10: keep-alive enable (1=on, 0=off)
uint8_t regS11       = 5;     // S11: keep-alive interval (seconds)
uint8_t regS12       = 3;     // S12: missed pongs before dropping connection
uint8_t regS13       = 1;     // S13: flow control 0=none, 1=XON/XOFF (default ON)
uint8_t regS14       = 1;     // S14: busy detect enable (1=on, 0=off)
uint8_t regS15       = 50;    // S15: channel scan duration in ms (default 50)

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
uint8_t  ringCount      = 0;           // how many RING responses sent so far
unsigned long ringTimer = 0;           // millis() of next RING output

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

// ACK mode
bool    hwAck = false;  // false = software flow (ATSHWACK=0, default); true = hardware ACK (ATSHWACK=1)

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
#define SW_RETX_MS  150   // retransmit slot if no SWACK within 150 ms

struct SwSlot {
    uint8_t  pkt[PAYLOAD_SIZE];
    uint32_t sentMs;
    bool     used;
};
SwSlot   swWin[SW_WIN_SIZE];
uint8_t  swWinHead = 0;           // next slot to fill
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
    { uint8_t v = EEPROM.read(EE_HWACK); hwAck = (v == 0xFF) ? false : (v != 0); }
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
    regS7 = EEPROM.read(EE_S7); if (regS7 == 0xFF) regS7 = 1;
    regS8 = EEPROM.read(EE_S8); if (regS8 == 0xFF) regS8 = 3;
    regS9  = EEPROM.read(EE_S9);  if (regS9  == 0xFF) regS9  = 10;
    regS10 = EEPROM.read(EE_S10); if (regS10 == 0xFF) regS10 = 1;
    regS11 = EEPROM.read(EE_S11); if (regS11 == 0xFF) regS11 = 5;
    regS12 = EEPROM.read(EE_S12); if (regS12 == 0xFF) regS12 = 3;
    regS13 = EEPROM.read(EE_S13); if (regS13 > 1)       regS13 = 1;
    regS14 = EEPROM.read(EE_S14); if (regS14 > 1)       regS14 = 1;
    regS15 = EEPROM.read(EE_S15); if (regS15 == 0 || regS15 == 0xFF) regS15 = 50;
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
    EEPROM.write(EE_HWACK,    hwAck ? 1 : 0);
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
    if (hwAck) {
        radio.setAutoAck(true);
        radio.setRetries(5, 15);   // 5 × 250 µs delay, up to 15 retries
    } else {
        radio.setAutoAck(false);
        radio.setRetries(0, 0);    // no HW retries in SWFLOW mode
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
    return txBuf[txTail++];
    txTail %= TX_BUF_SIZE;
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
void sendOK()     { Serial.println(F("OK")); }
void sendError()  { Serial.println(F("ERROR")); }
void sendNoCarrier() { Serial.println(F("NO CARRIER")); ledFlashER(); }
void sendConnect()   { Serial.println(F("CONNECT")); }
void sendRing()      { Serial.println(F("RING")); }

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
    if (!hwAck && type == PKT_DATA) {
        pkt[1] = dataTxSeq;          // overwrite with data-only seq
        uint8_t slotIdx = swWinHead % SW_WIN_SIZE;
        memcpy(swWin[slotIdx].pkt, pkt, PAYLOAD_SIZE);
        swWin[slotIdx].sentMs = millis();
        swWin[slotIdx].used   = true;
        dataTxSeq++;
        swWinHead++;
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
    // In HWACK mode yieldToRemote is never set so this path is never taken.
    if (yieldToRemote) {
        yieldToRemote = false;
        return;
    }

    while (txAvail() > 0) {
        // In SWFLOW mode, stop sending new packets when the window is full.
        if (!hwAck) {
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
                swflowAckData(pkt[1]);   // no-op in HWACK mode
                for (uint8_t i = 0; i < len && i < MAX_DATA; i++) {
                    rxPush(pkt[DATA_OFFSET + i]);
                }
                checkFlowControl();
            }
            break;

        case PKT_CONN:
            // Incoming connection request
            if (state == S_IDLE) {
                // Store caller MAC from packet payload.
                if (len >= 3) {
                    pendingMac[0] = pkt[DATA_OFFSET];
                    pendingMac[1] = pkt[DATA_OFFSET + 1];
                    pendingMac[2] = pkt[DATA_OFFSET + 2];
                } else {
                    memset(pendingMac, 0, 3);
                }
                ringCount = 0;
                ringTimer = 0;        // fire first RING immediately next loop
                state     = S_RINGING;
                // Auto-answer will be checked in loop(); ATA handles manual answer.
            }
            break;

        case PKT_DISC:
            if (state == S_DATA || state == S_CONNECTED) {
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
            break;

        case PKT_XOFF:
            radioXoffRecv = true;
            break;

        case PKT_ACK:
            // handshake ack during ATD
            if (state == S_CONNECTING) {
                state         = S_DATA;
                kaInitiator   = true;   // we dialled — we own keep-alive
                kaMissed      = 0;
                kaWaitingPong = false;
                kaPingAt      = millis() + (unsigned long)regS11 * 1000UL;
                sendConnect();
            }
            break;

        case PKT_NACK:
            // Remote detected a gap — retransmit the requested seq from our window.
            if (!hwAck && len >= 1) {
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
            if (!hwAck && len >= 1) {
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
    if (hwAck) return;

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
    sendConnectAck();
    state         = S_DATA;
    kaInitiator   = false;  // we answered — remote owns keep-alive, we only reply
    kaMissed      = 0;
    kaWaitingPong = false;
    kaPingAt      = 0;      // answerer never initiates pings
    kaLastPingMs  = millis(); // arm watchdog from connect time
    sendConnect();
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
    channel     = 76;
    speedEnum   = 1;
    hwAck       = false;  // SWFLOW is default
    baudIdx     = BAUD_DEFAULT;

    // S-registers
    regS0 = 0;
    regS6 = 0;
    regS7 = 1;
    regS8  = 3;
    regS9  = 10;
    regS10 = 1;
    regS11 = 5;
    regS12 = 3;
    regS13 = 1;
    regS14 = 1;
    regS15 = 50;
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
    memset(lastDialStr, 0, sizeof(lastDialStr));
    kaInitiator    = false;
    kaMissed       = 0;
    kaWaitingPong  = false;
    kaPingAt       = 0;
    kaLastPingMs   = 0;
    yieldToRemote  = false;
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

// ── Channel busy detection ───────────────────────────────────────────────────
// Listens on the current channel for regS15 milliseconds, sampling the RPD
// (Received Power Detector) register once per millisecond. The RPD asserts
// when received signal strength exceeds -64 dBm — enough to catch any active
// nRF24L01 link on the same channel (data, ACKs, keep-alive pings, all count).
// Returns true if the channel appears occupied, false if clear.
bool channelIsBusy() {
    radio.startListening();
    unsigned long start = millis();
    uint8_t hits = 0;
    uint8_t samples = 0;
    while (millis() - start < (unsigned long)regS15) {
        if (radio.testRPD()) hits++;
        samples++;
        delay(1);
    }
    openListenPipes();   // restore normal pipe config
    // Busy if RPD fired on more than 20% of samples — reduces false positives
    // from brief noise while still catching sustained link traffic.
    return (hits > 0 && (hits * 100 / samples) >= 20);
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
        Serial.println(F("nRF24L01 AT Modem v1.0"));

        // Radio hardware status — re-check live so ATI always reflects reality.
        bool chipOk = radio.isChipConnected();
        if (radioFailed || !chipOk) {
            Serial.println(F("Radio   : FAULT - module not responding"));
        } else {
            Serial.println(F("Radio   : OK"));
        }

        Serial.print(F("Baud    : ")); Serial.println(baudTable[baudIdx]);
        Serial.print(F("ACK mode: ")); Serial.println(hwAck ? F("HWACK (ATSHWACK=1)") : F("SWFLOW (ATSHWACK=0)"));
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
        // Clear SWFLOW window and cancel any pending retry on hangup.
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

            // S6: pre-dial wait.
            uint8_t prewait = (regS6 > 10) ? 10 : regS6;
            if (prewait > 0) delay((uint16_t)prewait * 1000UL);

            // S14: busy detect — scan channel before dialling.
            if (regS14 == 1) {
                if (channelIsBusy()) {
                    Serial.println(F("BUSY"));
                    ledFlashER();
                    // Do not set S_CONNECTING — leave IDLE, no retry.
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

    // ── ATSHWACK? / ATSHWACK=n ─────────────────────────────────────────────
    if (strcmp(uc, "ATSHWACK?") == 0) {
        Serial.println(hwAck ? F("1 (hardware ACK)") : F("0 (software flow control)"));
        sendOK();
        return;
    }
    if (strncmp(uc, "ATSHWACK=", 9) == 0) {
        int val = atoi(uc + 9);
        if (val == 0 || val == 1) {
            hwAck = (val == 1);
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
        Serial.println(F("Factory reset complete."));
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
        Serial.println(F("ERROR: nRF24L01 disconnected."));

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

    Serial.println(F("nRF24L01 AT Modem ready."));
    Serial.println(F("Type ATI for status."));

    if (startupSlot < DIAL_SLOTS && dialStr[startupSlot][0] != '\0') {
        autoDial   = true;
        autoDialMs = millis() + 2000;
        Serial.print(F("Autodial: slot "));
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
            Serial.print(F("Autodialling: "));
            Serial.println(dialStr[startupSlot]);
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
                // Third '+' received — start post-guard timer
                // We don't push '+' chars to TX (they're escape, not data)
                // Nothing else to do here; escape confirmed after 1 s below
            } else if (!escapeArmed) {
                txPush(b);
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
                Serial.print(F("NO CARRIER - retry "));
                Serial.print(dialRetryCount);
                Serial.print(F("/"));
                Serial.println(regS8);
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
        Serial.print(F("Redialling (attempt "));
        Serial.print(dialRetryCount);
        Serial.print(F("/"));
        Serial.print(regS8);
        Serial.println(F(")..."));
        processCommand(lastDialStr);
    }

    // ── 5. Receive radio packets ─────────────────────────────────────────────
    if (radio.available()) {
        uint8_t pkt[PAYLOAD_SIZE];
        radio.read(pkt, PAYLOAD_SIZE);
        handleRadioPacket(pkt);
    }

    // ── 6. Drain RX buffer → serial ─────────────────────────────────────────
    while (rxAvail() > 0) {
        int b = rxPop();
        if (b < 0) break;
        Serial.write((uint8_t)b);
    }

    // ── 7. Flush TX buffer → radio ───────────────────────────────────────────
    flushTxBuffer();

    // ── 8. Flow control check ────────────────────────────────────────────────
    checkFlowControl();

    // ── 9. Ring cadence and auto-answer ────────────────────────────────────
    if (state == S_RINGING && (ringTimer == 0 || now >= ringTimer)) {
        ringCount++;
        sendRing();
        ringTimer = now + 2000;   // Hayes standard: RING every ~2 s

        // Auto-answer if S0 is set and we've hit the ring threshold.
        if (regS0 > 0 && ringCount >= regS0) {
            doAnswer();
        }
    }

    // ── 10. Keep-alive tick ──────────────────────────────────────────────────
    if (regS10 && kaInitiator && (state == S_DATA || state == S_CONNECTED)) {
        if (kaPingAt == 0)
            kaPingAt = now + (unsigned long)regS11 * 1000UL;

        if (now >= kaPingAt) {
            if (kaWaitingPong) {
                kaMissed++;
                Serial.print(F("KA miss "));
                Serial.print(kaMissed);
                Serial.print('/');
                Serial.println(regS12);
                ledFlashER();
                if (kaMissed >= regS12) {
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
                Serial.println(F("KA timeout - no pings received"));
                ledFlashER();
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
    if (!hwAck && (state == S_DATA || state == S_CONNECTED)) {
        for (uint8_t i = 0; i < SW_WIN_SIZE; i++) {
            if (swWin[i].used && (now - swWin[i].sentMs) >= SW_RETX_MS) {
                openWritePipe(remoteMac);
                bool ok = radio.write(swWin[i].pkt, PAYLOAD_SIZE);
                if (ok) ledFlashSD(); else ledFlashER();
                openListenPipes();
                swWin[i].sentMs = now;
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

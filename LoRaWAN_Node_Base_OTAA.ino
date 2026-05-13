/*
 * || Project:          IoT Energy Meter with C/C++/FreeRTOS, Java/Spring,
 * ||                   TypeScript/Angular and Dart/Flutter.
 * || About:            End-to-end LoRaWAN network for monitoring electrical quantities.
 * || Version:          1.0
 * || Hardware:         ESP32 + RFM95W
 * || LoRaWAN Stack:    MCCI Arduino LMiC 6.0.1
 * || Network Server:   ChirpStack — AU915, sub-band 1 (ch 8–15 + 65)
 * || Activation:       OTAA
 * || Author:           Adail dos Santos Silva
 * || E-mail:           adail101@hotmail.com
 * || WhatsApp:         +55 89 9 9412-9256
 * ||
 * || SPDX-License-Identifier: MIT
 */

/********************************************************************
 _____              __ _                       _   _
/  __ \            / _(_)                     | | (_)
| /  \/ ___  _ __ | |_ _  __ _ _   _ _ __ __ _| |_ _  ___  _ __
| |    / _ \| '_ \|  _| |/ _` | | | | '__/ _` | __| |/ _ \| '_ \
| \__/\ (_) | | | | | | | (_| | |_| | | | (_| | |_| | (_) | | | |
 \____/\___/|_| |_|_| |_|\__, |\__,_|_|  \__,_|\__|_|\___/|_| |_|
                          __/ |
                         |___/
********************************************************************/

#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>

/* ------------------------------------------------------------------ */
/* Hardware pin mapping — ESP32                                       */
/* ------------------------------------------------------------------ */
#define PIN_SCLK    18
#define PIN_MISO    19
#define PIN_MOSI    23
#define PIN_NSS      5
#define PIN_RESET   14
#define PIN_DIO0    34
#define PIN_DIO1    35  /* tied but not used by LMiC on this board    */
#define PIN_DIO2    39  /* not connected — kept for reference         */

/* ------------------------------------------------------------------ */
/* LoRaWAN credentials — OTAA (ChirpStack AU915, sub-band 1)          */
/* All values are big-endian (MSB first) as required by LMiC.         */
/* ------------------------------------------------------------------ */

/* AppEUI  MSB: b2 f6 34 a1 e6 2c 10 c7 */
static const u1_t PROGMEM APPEUI[8]  = { 0xb2, 0xf6, 0x34, 0xa1, 0xe6, 0x2c, 0x10, 0xc7 };

/* DevEUI  MSB: de 63 1e a3 e5 f5 8d f3  (use this value in ChirpStack) */
static const u1_t PROGMEM DEVEUI[8]  = { 0xde, 0x63, 0x1e, 0xa3, 0xe5, 0xf5, 0x8d, 0xf3 };

/* AppKey  MSB: 6f c5 7c 4c da 46 dd c5 02 bc b7 85 5e 4e 94 ae */
static const u1_t PROGMEM APPKEY[16] = { 0x6f, 0xc5, 0x7c, 0x4c, 0xda, 0x46, 0xdd, 0xc5, 0x02, 0xbc, 0xb7, 0x85, 0x5e, 0x4e, 0x94, 0xae };

/* LMiC callbacks — copy keys from PROGMEM into RAM buffers. */
void os_getArtEui(u1_t *buf) { memcpy_P(buf, APPEUI,  8); }
void os_getDevEui(u1_t *buf) { memcpy_P(buf, DEVEUI,  8); }
void os_getDevKey(u1_t *buf) { memcpy_P(buf, APPKEY, 16); }

/* ------------------------------------------------------------------ */
/* Remote reset command — downlink protocol                           */
/*                                                                    */
/* FPort : 101 (0x65)                                                 */
/*                                                                    */
/* Payload structure (8 bytes total):                                 */
/*                                                                    */
/*  Byte:  0     1     2     3     4     5     6     7                */
/*        ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐           */
/*        │ HDR │ HDR │ CMD │ CMD │ CMD │ CMD │TAIL │TAIL │           */
/*        │0xAD │0xA1 │0xDE │0xAD │0xBE │0xEF │0x0D │0x0A │           */
/*        └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘           */
/*                                                                    */
/*  Header  (2 bytes): 0xAD 0xA1  — "AdAil" signature                 */
/*  Command (4 bytes): 0xDE 0xAD 0xBE 0xEF — reset magic word         */
/*  Tail    (2 bytes): 0x0D 0x0A  — CRLF end-of-frame marker          */
/*                                                                    */
/* Any byte mismatch causes the command to be silently ignored.       */
/* The reset is deferred by RESET_DELAY_MS to allow the ACK           */
/* (if confirmed downlink) to be transmitted before rebooting.        */
/* ------------------------------------------------------------------ */
#define RESET_FPORT          101

#define RESET_CMD_LEN          8

#define RESET_HDR_0         0xAD
#define RESET_HDR_1         0xA1
#define RESET_CMD_0         0xDE
#define RESET_CMD_1         0xAD
#define RESET_CMD_2         0xBE
#define RESET_CMD_3         0xEF
#define RESET_TAIL_0        0x0D
#define RESET_TAIL_1        0x0A

#define RESET_DELAY_MS      2000  /* ms to wait before rebooting      */

/* ------------------------------------------------------------------ */
/* Application configuration                                          */
/* ------------------------------------------------------------------ */

/* Uplink payload — replace with real sensor data in production.      */
static uint8_t payload[] = "AdailSilva-IoT";

/*
 * Uplink interval in seconds.
 * Respect the AU915 1 % duty-cycle limit — do not set below 10 s
 * when using SF7/BW125.  The LMiC enforces the limit regardless,
 * but a reasonable base keeps scheduling predictable.
 */
static const uint32_t TX_INTERVAL_S = 10;

/* LMiC job handle for the periodic uplink. */
static osjob_t txjob;

/* ------------------------------------------------------------------ */
/* Pin mapping struct                                                 */
/* ------------------------------------------------------------------ */
const lmic_pinmap lmic_pins = {
    .nss   = PIN_NSS,
    .rxtx  = LMIC_UNUSED_PIN,
    .rst   = PIN_RESET,
    /* DIO2 is not required for LoRa mode on the RFM95W. */
    .dio   = { PIN_DIO0, PIN_DIO1, LMIC_UNUSED_PIN },
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                               */
/* ------------------------------------------------------------------ */
void do_send(osjob_t *j);

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/* Print a byte as two hex digits with a leading zero if needed. */
static void printHex2(uint8_t v) {
    if (v < 0x10) Serial.print('0');
    Serial.print(v, HEX);
}

/* Print a key array as XX-XX-XX-… */
static void printKey(const uint8_t *key, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (i) Serial.print('-');
        printHex2(key[i]);
    }
    Serial.println();
}

/* Print the session keys obtained after a successful JOIN. */
static void printSessionKeys(void) {
    u4_t      netid   = 0;
    devaddr_t devaddr = 0;
    u1_t      nwkKey[16];
    u1_t      artKey[16];

    LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);

    Serial.print(F("  netid:   ")); Serial.println(netid, DEC);
    Serial.print(F("  devaddr: 0x")); Serial.println(devaddr, HEX);
    Serial.print(F("  AppSKey: ")); printKey(artKey, sizeof(artKey));
    Serial.print(F("  NwkSKey: ")); printKey(nwkKey, sizeof(nwkKey));
}

/* ------------------------------------------------------------------ */
/* Remote reset handler                                               */
/*                                                                    */
/* Validates the 8-byte reset command received on FPort 101.          */
/* Returns true if the payload matches the expected pattern exactly.  */
/* ------------------------------------------------------------------ */
static bool isResetCommand(const uint8_t *data, uint8_t len) {
    if (len != RESET_CMD_LEN) return false;

    return (data[0] == RESET_HDR_0 &&
            data[1] == RESET_HDR_1 &&
            data[2] == RESET_CMD_0 &&
            data[3] == RESET_CMD_1 &&
            data[4] == RESET_CMD_2 &&
            data[5] == RESET_CMD_3 &&
            data[6] == RESET_TAIL_0 &&
            data[7] == RESET_TAIL_1);
}

static void handleDownlink(void) {
    if (LMIC.dataLen == 0) return;

    uint8_t  fport = LMIC.frame[LMIC.dataBeg - 1];
    uint8_t  len   = LMIC.dataLen;
    uint8_t *data  = &LMIC.frame[LMIC.dataBeg];

    /* Print every downlink regardless of port. */
    Serial.print(F("  Downlink received — "));
    Serial.print(len);
    Serial.print(F(" byte(s) on FPort "));
    Serial.print(fport);
    Serial.print(F(": "));
    for (uint8_t i = 0; i < len; ++i) {
        printHex2(data[i]);
        Serial.print(' ');
    }
    Serial.println();

    /* ---------------------------------------------------------- */
    /* Remote reset command — FPort 101                           */
    /* ---------------------------------------------------------- */
    if (fport == RESET_FPORT) {
        Serial.println(F("  [RESET PORT] Command received on FPort 101 — validating..."));

        if (isResetCommand(data, len)) {
            Serial.println(F("  [RESET] Valid reset command — rebooting in 2 s..."));
            Serial.flush();
            delay(RESET_DELAY_MS);
            esp_restart();
        } else {
            Serial.println(F("  [RESET] Invalid payload — command ignored."));
            Serial.print(F("  Expected: "));
            Serial.printf("%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                RESET_HDR_0, RESET_HDR_1,
                RESET_CMD_0, RESET_CMD_1, RESET_CMD_2, RESET_CMD_3,
                RESET_TAIL_0, RESET_TAIL_1);
            Serial.print(F("  Received: "));
            for (uint8_t i = 0; i < len; ++i) {
                printHex2(data[i]);
                if (i < len - 1) Serial.print(' ');
            }
            Serial.println();
        }
    }
}

/* ------------------------------------------------------------------ */
/* Session diagnostics — called on EV_TXSTART                         */
/* ------------------------------------------------------------------ */
static void print_info_session(void) {
    Serial.print(F("LMiC NwkSKey: "));
    for (int i = 0; i < 16; i++) {
        if (LMIC.nwkKey[i] < 0x10) Serial.print('0');
        Serial.print(LMIC.nwkKey[i], HEX);
    }
    Serial.println();

    Serial.print(F("LMiC AppSKey: "));
    for (int i = 0; i < 16; i++) {
        if (LMIC.artKey[i] < 0x10) Serial.print('0');
        Serial.print(LMIC.artKey[i], HEX);
    }
    Serial.println();

    Serial.print(F("LMiC DevAddr: 0x"));
    Serial.println(LMIC.devaddr, HEX);
    Serial.println();

    Serial.print(F("Frame raw: "));
    for (int i = 0; i < LMIC.dataLen + LMIC.dataBeg; i++) {
        if (LMIC.frame[i] < 0x10) Serial.print('0');
        Serial.print(LMIC.frame[i], HEX);
        Serial.print(' ');
    }
    Serial.println();

    Serial.print(F("FCnt/seqnoUp: ")); Serial.println(LMIC.seqnoUp);
    Serial.print(F("FCnt/seqnoDn: ")); Serial.println(LMIC.seqnoDn);

    /* Print MIC — last 4 bytes of the frame. */
    uint16_t frameLen = LMIC.dataBeg + LMIC.dataLen;
    if (frameLen >= 4) {
        Serial.print(F("  MIC: "));
        for (uint8_t i = frameLen - 4; i < frameLen; i++) {
            if (LMIC.frame[i] < 0x10) Serial.print('0');
            Serial.print(LMIC.frame[i], HEX);
            if (i < frameLen - 1) Serial.print('-');
        }
        Serial.println();
    }
}

/* ------------------------------------------------------------------ */
/* LMiC event handler                                                 */
/* ------------------------------------------------------------------ */

/*****************************
 _____           _
/  __ \         | |
| /  \/ ___   __| | ___
| |    / _ \ / _` |/ _ \
| \__/\ (_) | (_| |  __/
 \____/\___/ \__,_|\___|
*****************************/

void onEvent(ev_t ev) {
    //Serial.print(os_getTime());
    //Serial.print(F(": "));

    switch (ev) {

        case EV_JOINING:
            Serial.println(F("EV_JOINING"));
            break;

        case EV_JOIN_TXCOMPLETE:
            /* No JoinAccept received in the join windows — LMiC will retry. */
            Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
            break;

        case EV_JOIN_FAILED:
            Serial.println(F("EV_JOIN_FAILED"));
            break;

        case EV_REJOIN_FAILED:
            Serial.println(F("EV_REJOIN_FAILED"));
            break;

        case EV_JOINED:
            Serial.println(F("EV_JOINED"));
            printSessionKeys();
            /*
             * Disable link-check mode.  It is automatically enabled
             * after JOIN, but it can force the stack to use lower
             * data rates and shrink the maximum payload size.
             * Must be called here AND in setup() for OTAA.
             */
            LMIC_setLinkCheckMode(0);
            break;

        case EV_TXSTART:
            Serial.println();
            Serial.println(F("EV_TXSTART"));
            print_info_session();
            break;

        case EV_RXSTART:
            /*
             * Do NOT print here — any Serial output during RX setup
             * consumes time and can mis-align the receive window.
             */
            break;

        case EV_TXCOMPLETE:
            Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));

            if (LMIC.txrxFlags & TXRX_ACK) {
                Serial.println(F("  ACK received."));
            }

            /*
             * Handle downlink — including the remote reset command.
             * Called here because LMIC.dataLen and LMIC.frame are
             * only valid inside EV_TXCOMPLETE.
             */
            handleDownlink();

            /* Schedule the next uplink. */
            os_setTimedCallback(&txjob,
                                os_getTime() + sec2osticks(TX_INTERVAL_S),
                                do_send);
            break;

        case EV_TXCANCELED:
            Serial.println(F("EV_TXCANCELED"));
            break;

        case EV_LINK_DEAD:
            /*
             * Fired when the network has not acknowledged several
             * consecutive uplinks.
             */
            Serial.println(F("EV_LINK_DEAD"));
            break;

        case EV_LINK_ALIVE:
            Serial.println(F("EV_LINK_ALIVE"));
            break;

        case EV_RESET:
            Serial.println(F("EV_RESET"));
            break;

        case EV_RXCOMPLETE:
            /* Class B ping-slot reception (not used here). */
            Serial.println(F("EV_RXCOMPLETE"));
            break;

        case EV_SCAN_TIMEOUT:   Serial.println(F("EV_SCAN_TIMEOUT"));   break;
        case EV_BEACON_FOUND:   Serial.println(F("EV_BEACON_FOUND"));   break;
        case EV_BEACON_MISSED:  Serial.println(F("EV_BEACON_MISSED"));  break;
        case EV_BEACON_TRACKED: Serial.println(F("EV_BEACON_TRACKED")); break;
        case EV_LOST_TSYNC:     Serial.println(F("EV_LOST_TSYNC"));     break;

        default:
            Serial.print(F("Unknown event: "));
            Serial.println((unsigned)ev);
            break;
    }
}

/* ------------------------------------------------------------------ */
/* Uplink function                                                    */
/* ------------------------------------------------------------------ */

void do_send(osjob_t *j) {
    if (LMIC.opmode & OP_TXRXPEND) {
        Serial.println(F("OP_TXRXPEND — previous job still running, skipping."));
        return;
    }

    // Força limpeza de qualquer frame em cache.
    //LMIC_clrTxData();

    // Reseta o frame counter explicitamente.
    //LMIC.seqnoUp = 0;
    //LMIC.seqnoDn = 0;

    // Força payload diferente a cada envio.
    //static uint8_t counter = 0;
    //payload[0] = counter++;  // Altera o primeiro byte.

    /* TODO: read real sensor data into payload[] before calling setTxData2. */
    LMIC_setTxData2(1, payload, sizeof(payload) - 1, 0 /* unconfirmed */);
    Serial.println(F("Packet queued."));
}

/* ------------------------------------------------------------------ */
/* Setup                                                              */
/* ------------------------------------------------------------------ */

/*****************************
 _____      _
/  ___|    | |
\ `--.  ___| |_ _   _ _ __
 `--. \/ _ \ __| | | | '_ \
/\__/ /  __/ |_| |_| | |_) |
\____/ \___|\__|\__,_| .__/
                     | |
                     |_|
*****************************/

void setup() {
    Serial.begin(9600);
    delay(100);

    /* Explicit SPI initialisation required on ESP32. */
    SPI.begin(PIN_SCLK, PIN_MISO, PIN_MOSI, PIN_NSS);

    Serial.println(F("Starting — ChirpStack AU915 OTAA"));

#ifdef VCC_ENABLE
    /* Pinoccio Scout power rail enable. */
    pinMode(VCC_ENABLE, OUTPUT);
    digitalWrite(VCC_ENABLE, HIGH);
    delay(1000);
#endif

    os_init();
    LMIC_reset();

    /* -------------------------------------------------------------- */
    /* Channel plan — ChirpStack AU915, sub-band 1                    */
    /* Uplink:   ch  8–15  (916.8–918.2 MHz, 200 kHz, BW125)          */
    /* Uplink:   ch 65     (917.5 MHz, BW500) — JOIN / ADR            */
    /* Downlink: ch  0– 7  (923.3–924.7 MHz, BW500) — managed by NS   */
    /* -------------------------------------------------------------- */
    for (u1_t b  =  0; b  <  8; ++b)   LMIC_disableSubBand(b);
    for (u1_t ch =  0; ch < 72; ++ch)  LMIC_disableChannel(ch);
    for (u1_t ch =  8; ch <= 15; ++ch) LMIC_enableChannel(ch);
    LMIC_enableChannel(65);

    /* Adaptive Data Rate off — fixed SF7/BW125/14 dBm. */
    LMIC_setAdrMode(0);

    /*
     * Link-check mode is disabled here AND again in EV_JOINED
     * because the JOIN procedure re-enables it automatically.
     */
    LMIC_setLinkCheckMode(0);

    /* DR5 = SF7/BW125 for AU915 — highest data rate, lowest range.   */
    LMIC_setDrTxpow(DR_SF7, 14);

    /*
     * RX2 window — AU915/ChirpStack default:
     *   923.3 MHz, SF12/BW500 → DR_SF12CR (DR8).
     * The JOIN Accept CFList sets this automatically in OTAA.
     * Defined explicitly here as a safeguard.
     */
    LMIC.dn2Dr = DR_SF12CR;

    /*
     * Clock-error compensation.
     * The ESP32 XTAL is accurate enough that 1 % is sufficient to
     * widen the RX preamble detection window without introducing a
     * significant delay before the window opens.
     */
    LMIC_setClockError(MAX_CLOCK_ERROR * 1 / 100);

    /* Kick off the first uplink — also triggers the OTAA JOIN. */
    do_send(&txjob);
}

/* ------------------------------------------------------------------ */
/* Main loop — hand control to the LMiC cooperative scheduler         */
/* ------------------------------------------------------------------ */

/*****************************
 _
| |
| |     ___   ___  _ __
| |    / _ \ / _ \| '_ \
| |___| (_) | (_) | |_) |
\_____/\___/ \___/| .__/
                  | |
                  |_|
*****************************/

void loop() {
    os_runloop_once();
}

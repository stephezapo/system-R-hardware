/**
 * TLC5947 – slow RGB rainbow on channels 0, 1, 2
 * Hardware: Raspberry Pi Pico + Adafruit TLC5947 breakout
 *
 * Wiring (matches schematic):
 *   GP18 → CLK
 *   GP19 → MOSI (DAT)
 *   GP17 → LAT  (latch)
 *   GP16 → OE   (output enable, active LOW)
 *   3.3V → VCC
 *   GND  → GND
 *   5V   → V+   (LED supply, common anode)
 *   One R_IREF resistor (~1.5 kΩ) on the IREF pin sets 20 mA per channel.
 *
 * The TLC5947 has 24 × 12-bit PWM channels (0–4095).
 * Channels 0/1/2 drive R/G/B; all others are left at 0.
 */

#include <SPI.h>

// ---------- pin definitions ----------
static const uint8_t PIN_CLK   = 18;
static const uint8_t PIN_MOSI  = 19;
static const uint8_t PIN_LAT   = 17;
static const uint8_t PIN_OE    = 16;

// ---------- TLC5947 constants ----------
static const uint8_t  NUM_CHANNELS = 24;
static const uint16_t MAX_VAL      = 4095;

// 24 channels × 12 bit = 288 bit = 36 bytes
static uint8_t tlcBuf[36];

// ---------- helpers ----------

/**
 * Pack 24 twelve-bit values into the 36-byte SPI buffer.
 * The TLC5947 expects channel 23 first (MSB), channel 0 last.
 */
void packBuffer(uint16_t values[NUM_CHANNELS]) {
  memset(tlcBuf, 0, sizeof(tlcBuf));
  for (int ch = 0; ch < NUM_CHANNELS; ch++) {
    uint16_t v = constrain(values[ch], 0, MAX_VAL);
    // reversed channel index: ch 23 goes to byte 0
    int idx = NUM_CHANNELS - 1 - ch;
    int bytePos = (idx * 12) / 8;
    int bitOff  = (idx * 12) % 8;

    if (bitOff == 0) {
      tlcBuf[bytePos]     = (v >> 4) & 0xFF;
      tlcBuf[bytePos + 1] = (v << 4) & 0xFF;
    } else { // bitOff == 4
      tlcBuf[bytePos]    |= (v >> 8) & 0x0F;
      tlcBuf[bytePos + 1] = v & 0xFF;
    }
  }
}

/** Send buffer to TLC5947 and pulse the latch. */
void writeTLC(uint16_t values[NUM_CHANNELS]) {
  packBuffer(values);
  SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
  digitalWrite(PIN_LAT, LOW);
  SPI.transfer(tlcBuf, sizeof(tlcBuf));
  // latch pulse: HIGH then LOW
  digitalWrite(PIN_LAT, HIGH);
  delayMicroseconds(1);
  digitalWrite(PIN_LAT, LOW);
  SPI.endTransaction();
}

/**
 * HSV → RGB conversion, all values 0–4095.
 * hue: 0–4095 wraps the full colour wheel.
 */
void hsvToRgb12(uint16_t hue, uint16_t sat, uint16_t val,
                uint16_t &r, uint16_t &g, uint16_t &b) {
  // map hue to 0–5 sector + fractional part (0–4095)
  uint32_t h6   = (uint32_t)hue * 6;          // 0 – 6*4095
  uint8_t  sector = h6 / 4096;                 // 0..5
  uint32_t frac   = h6 % 4096;                 // 0..4095

  uint32_t p = (uint32_t)val * (4095 - sat) / 4095;
  uint32_t q = (uint32_t)val * (4095 - (uint32_t)sat * frac / 4096) / 4095;
  uint32_t t = (uint32_t)val * (4095 - (uint32_t)sat * (4095 - frac) / 4096) / 4095;

  switch (sector) {
    case 0: r = val; g = t;   b = p;   break;
    case 1: r = q;   g = val; b = p;   break;
    case 2: r = p;   g = val; b = t;   break;
    case 3: r = p;   g = q;   b = val; break;
    case 4: r = t;   g = p;   b = val; break;
    default:r = val; g = p;   b = q;   break;
  }
}

// ---------- Arduino lifecycle ----------

void setup() {
  pinMode(PIN_LAT, OUTPUT);
  pinMode(PIN_OE,  OUTPUT);
  digitalWrite(PIN_LAT, LOW);
  digitalWrite(PIN_OE,  LOW);   // OE LOW = outputs enabled

  // On arduino-pico the SPI pins must be set before begin()
  //SPI.setRX(PIN_MOSI);          // not used but keeps the core happy
  SPI.setTX(PIN_MOSI);
  SPI.setSCK(PIN_CLK);
  SPI.begin();

  // Blank all channels on startup
  uint16_t blank[NUM_CHANNELS] = {0};
  writeTLC(blank);
}

void loop() {
  // Rainbow period: how many milliseconds for one full colour rotation
  static const uint32_t PERIOD_MS = 4000UL;

  uint32_t t   = millis() % PERIOD_MS;
  uint16_t hue = (uint32_t)MAX_VAL * t / PERIOD_MS;   // 0–4095

  uint16_t r, g, b;
  hsvToRgb12(hue, MAX_VAL, MAX_VAL, r, g, b);

  uint16_t channels[NUM_CHANNELS] = {0};
  channels[0] = 1;   // RED   on OUT0
  channels[1] = 10;   // GREEN on OUT1
  channels[2] = 100;   // BLUE  on OUT2

  writeTLC(channels);
  delay(16);   // ~60 fps update rate, smooth enough for a slow fade
}

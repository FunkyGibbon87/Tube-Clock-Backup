// Version 2.1 for NANO - non-blocking rewrite of v1.2
// Working with real GPS Sync
// 040324 Updated numberals 1 and 7. Added Test for 0 seconds in NEMA string
// 050324 Animated numerals. Looks good but breaks if I use more than 7 incements
// 090324 Added Conrads Byte shifiting function and optimised the loop by calling the function.
// Converted lookup to 2 dimensional array
// 100324 Fitted 100nF caps to each shift register and a 10uF across board power.
// 120324 Changed TimeString4 to 6 and removed 2 x %02d's from the main loop FIXED Crashing!!!
// 130524 Tried WaveShare ESP32 - No Go. Code is Solid and running for 2 months with no issues on Nano Clone
// 170825 Tube Clock running for 15 Months no issues.
// 210126 Tube Clock running for 20 months no issues
// 121126 Tube Clock running with no issues
// 121126 v2.0 No delay() anywhere - display runs as a millis() state machine.
//        GPS now decoded by TinyGPS++ (checksums checked, needs a real fix).
//        RTC set from GPS UTC + timezone, date rolls over properly.
//        Automatic GPS resync every night at 03:00.
//        Boot sync gives up after 5 minutes and runs on the RTC (unless the RTC lost power).
//        Removed unused pins 11/12, SPI, Wire.begin and the String handling.
// 121126 v2.1 Starts on the first satellite it hears - no full position fix needed.

#include <Arduino.h>
#include <RTClib.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>

TinyGPSPlus gps;   // The TinyGPS++ object - now actually fed with GPS data
RTC_DS3231 rtc;

const int RX_PIN = 9, TX_PIN = 10;   // GPS Pins
SoftwareSerial gps_serial(RX_PIN, TX_PIN);

const int latchPin = 4;   // ST_CP of 74HC595 (latch pin)
const int clockPin = 5;   // SH_CP of 74HC595 (clock pin)
const int dataPin  = 2;   // DS of 74HC595 (data pin)

// ---------------- Settings ----------------
const long TIMEZONE_SECONDS = 10L * 3600L;               // Brisbane, UTC+10, no daylight saving
const unsigned int W = 40;                               // Delay between each layer of the digit (ms)
const unsigned long BLANK_BETWEEN_MS = 1600;             // Blank time between each HHMM display block
const unsigned long BOOT_SYNC_TIMEOUT_MS = 5UL * 60UL * 1000UL;  // Give up waiting for GPS at boot after 5 min
const uint8_t RESYNC_HOUR = 3;                           // Nightly resync starts at 03:00 local

// +++++++++++++ Segment definitions (unchanged from v1.2)
// Rows = layers A..J, columns = 0-9, 10 = All On, 11 = Blank, 12 = WiFi Symbol
const uint8_t COL_BLANK = 11;
const uint8_t COL_WIFI  = 12;

byte SegDisplay[10][13] = {
{B00101110,B11110110,B00011110,B10001110,B01011101,B10011100,B11011100,B10101110,B00011100,B00011100,B00000000,B11111111,B11100011}, // A Seg
{B00110000,B10101011,B10110011,B00111011,B01111110,B00111010,B00111000,B11101111,B00111000,B00111110,B00000000,B11111111,B11101111}, // B Seg
{B01101111,B11110111,B01011111,B11001111,B01011101,B11011101,B11011101,B11101111,B01011101,B01011101,B00000000,B11111111,B11100011}, // C Seg
{B00110000,B10101011,B10110011,B00111011,B01111110,B00111010,B00111000,B11101111,B00111000,B00111110,B00000000,B11111111,B11100011}, // D Seg
{B11111111,B11111111,B11011111,B11011111,B11011101,B11011101,B11011101,B11111111,B11011101,B11011101,B00000000,B11111111,B11100011}, // E Seg
{B00110001,B10101011,B10110011,B00111011,B01111111,B00111011,B00111001,B11101111,B00111001,B00111111,B00000000,B11111111,B11100011}, // F Seg
{B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B00000000,B11111111,B11100011}, // G Seg
{B00110001,B10101011,B10110011,B00111011,B01111111,B00111011,B00111001,B11101111,B00111001,B00111111,B00000000,B11111111,B11100011}, // H Seg
{B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B11111111,B00000000,B11111111,B11100011}, // I Seg
{B10111011,B10111011,B10111011,B10111011,B11111111,B10111011,B10111011,B11111111,B10111011,B10111111,B00000000,B11111111,B11100011}  // J Seg
};

// +++++++++++++ The animation, as a table instead of 10 Shift2Bytes lines
// Same order and timing as v1.2: build up J/I -> B/A, hold 1 second, fade back down, then blank.
const uint8_t FRAME_COUNT = 10;
const uint8_t frameByte1Row[FRAME_COUNT]  = { 9, 7, 5, 3,    1, 3, 5, 7, 9,   1 };
const uint8_t frameByte2Row[FRAME_COUNT]  = { 8, 6, 4, 2,    0, 2, 4, 6, 8,   0 };
const unsigned int frameTime[FRAME_COUNT] = { W, W, W, W, 1000, W, W, W, W, 200 };
// The last frame always shows column 11 (Blank), the others show the digit.

// +++++++++++++ State
enum ClockMode { MODE_BOOT_SYNC, MODE_RUNNING };
ClockMode mode = MODE_BOOT_SYNC;
unsigned long bootStartMs = 0;
bool rtcLostPower = false;

uint8_t digits[4];             // H H M M
uint8_t digitIndex = 0;        // which of the 4 digits is showing
uint8_t frameIndex = 0;        // which animation frame of that digit
bool inBlankGap = true;        // true during the 1.6 s gap between display blocks
unsigned long stepStartMs = 0;
unsigned long stepLengthMs = 0;

bool resyncPending = false;
uint8_t lastResyncDay = 0;     // day of the month the nightly resync last ran


// Function: "Shift2Bytes" - sends one frame to the shift registers. No delay in here any more.
void Shift2Bytes(byte Byte1, byte Byte2)
{
  digitalWrite(latchPin, LOW);
  shiftOut(dataPin, clockPin, LSBFIRST, Byte1);   // First Half of Digit
  shiftOut(dataPin, clockPin, LSBFIRST, Byte2);   // Second Half of Digit
  digitalWrite(latchPin, HIGH);
}


// Hand every character waiting from the GPS to TinyGPS++. Takes microseconds, never waits.
void feedGPS()
{
  while (gps_serial.available() > 0) {
    gps.encode(gps_serial.read());
  }
}


// True when a fresh $GPRMC sentence (time + date) has arrived and its checksum passed.
// No position fix needed - the GPS knows the time from the first satellite it hears,
// so the clock starts on a weak signal. (Early time can be a few seconds off until the
// GPS learns the leap seconds - doesn't matter for HHMM, and the nightly resync tidies it.)
// The year check rejects empty time/date fields (they decode as 2000) and any junk default date.
bool gpsTimeIsGood()
{
  return gps.time.isUpdated() && gps.date.isUpdated()
      && gps.time.isValid()   && gps.date.isValid()
      && gps.time.age() < 1000
      && gps.date.year() >= 2024
      && gps.date.month() >= 1;
}


void printTime(const DateTime &t)
{
  if (t.hour() < 10)   Serial.print('0');
  Serial.print(t.hour());
  Serial.print(':');
  if (t.minute() < 10) Serial.print('0');
  Serial.print(t.minute());
  Serial.print(':');
  if (t.second() < 10) Serial.print('0');
  Serial.println(t.second());
}


// Set the RTC from GPS. GPS gives UTC; adding a TimeSpan handles the date rolling over properly.
void setRtcFromGps()
{
  DateTime utc(gps.date.year(), gps.date.month(), gps.date.day(),
               gps.time.hour(), gps.time.minute(), gps.time.second());
  DateTime local = utc + TimeSpan(TIMEZONE_SECONDS);
  rtc.adjust(local);
  Serial.print(F("RTC set from GPS: "));
  printTime(local);
}


// Start the nightly resync at 03:00. If there's no GPS fix by 04:00, give up until tomorrow.
void checkNightlyResync(const DateTime &now)
{
  if (now.hour() == RESYNC_HOUR && now.day() != lastResyncDay) {
    lastResyncDay = now.day();
    resyncPending = true;
    Serial.println(F("Nightly GPS resync started"));
  }
  if (resyncPending && now.hour() != RESYNC_HOUR) {
    resyncPending = false;
    Serial.println(F("Nightly resync: no GPS time, will try again tomorrow"));
  }
}


// Read the RTC once per display block and split HHMM into 4 digits. No strings needed.
void loadTimeDigits()
{
  DateTime now = rtc.now();
  digits[0] = now.hour() / 10;
  digits[1] = now.hour() % 10;
  digits[2] = now.minute() / 10;
  digits[3] = now.minute() % 10;

  Serial.print(F("Showing "));          // Serial Monitor heartbeat
  printTime(now);

  checkNightlyResync(now);
}


// Send the current frame to the tube and note how long it should stay there.
void showFrame()
{
  byte col = (frameIndex == FRAME_COUNT - 1) ? COL_BLANK : digits[digitIndex];
  Shift2Bytes(SegDisplay[frameByte1Row[frameIndex]][col],
              SegDisplay[frameByte2Row[frameIndex]][col]);
  stepStartMs  = millis();
  stepLengthMs = frameTime[frameIndex];
}


// The display engine. Called every pass of loop(); does nothing until the current frame's time is up.
void updateDisplay()
{
  if (millis() - stepStartMs < stepLengthMs) return;   // current frame still showing

  if (inBlankGap) {
    // Gap finished: get the new time and start the first digit
    inBlankGap = false;
    loadTimeDigits();
    digitIndex = 0;
    frameIndex = 0;
  } else {
    frameIndex++;
    if (frameIndex >= FRAME_COUNT) {          // finished this digit
      frameIndex = 0;
      digitIndex++;
      if (digitIndex >= 4) {                  // finished all 4 digits - start the blank gap
        inBlankGap = true;                    // (tube is already blank from the last frame)
        stepStartMs  = millis();
        stepLengthMs = BLANK_BETWEEN_MS;
        return;
      }
    }
  }
  showFrame();
}


void startClock()
{
  mode = MODE_RUNNING;
  inBlankGap = true;          // with a zero-length gap, the first digit shows straight away
  stepStartMs = millis();
  stepLengthMs = 0;
}


void setup()
{
  Serial.begin(9600);
  gps_serial.begin(9600);

  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(dataPin, OUTPUT);

  if (!rtc.begin()) {
    Serial.println(F("RTC not found - check wiring"));
  }
  rtcLostPower = rtc.lostPower();   // true if the RTC battery went flat and its time can't be trusted

  // Send the WiFi Symbol to the Tube. This stays until the GPS has the time (or we time out).
  Shift2Bytes(SegDisplay[1][COL_WIFI], SegDisplay[0][COL_WIFI]);
  Serial.println(F("Waiting for GPS time..."));
  bootStartMs = millis();
}


void loop()
{
  feedGPS();   // always keep reading the GPS, every pass

  if (mode == MODE_BOOT_SYNC) {
    if (gpsTimeIsGood()) {
      setRtcFromGps();
      startClock();
    } else if (!rtcLostPower && millis() - bootStartMs >= BOOT_SYNC_TIMEOUT_MS) {
      // RTC battery is fine, so its time is still good - don't wait forever
      Serial.println(F("No GPS time after 5 minutes - running on the RTC"));
      startClock();
    }
    return;
  }

  // Nightly resync happens quietly in the background - the display never stops
  if (resyncPending && gpsTimeIsGood()) {
    setRtcFromGps();
    resyncPending = false;
  }

  updateDisplay();
}
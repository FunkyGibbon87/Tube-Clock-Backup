// Version 1.4 - Adapted for Arduino Nano ESP32 (clean compile, no CPU freq tweak)
// Original by Gibbo - 20+ months stable on classic Nano
// 2026 tweak: Slow manual bit-banging for 74HC595 compatibility on ESP32
// Uses same pins: latch=4, clock=5, data=2
// Removed setCpuFrequencyMhz() as it requires non-standard include in Nano ESP32 core

#include <Arduino.h>
#include <RTClib.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>

TinyGPSPlus gps;
RTC_DS3231 rtc;

const int RX_PIN = 9;
const int TX_PIN = 10;
SoftwareSerial gps_serial(RX_PIN, TX_PIN);

const int TimeSync = 11;   // Pull low to force resync if needed
const int Adjust    = 12;  // Unused in current code but kept
const int timezone  = 10;  // Brisbane AEST hard-coded for now

int latchPin = 4;   // ST_CP of 74HC595
int clockPin = 5;   // SH_CP of 74HC595
int dataPin  = 2;   // DS  of 74HC595

int W = 40;         // Delay (ms) between animation layers

// Segment definitions: Two bytes per digit (16-segment style)
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

void Shift2Bytes(byte Byte1, byte Byte2, int Delay) {
  digitalWrite(latchPin, LOW);

  // Manual bit-bang Byte1 - LSBFIRST to match original
  for (int bit = 0; bit < 8; bit++) {
    digitalWrite(dataPin, (Byte1 >> bit) & 1);
    delayMicroseconds(40);          // Key fix: 20–80 µs; tune if needed
    digitalWrite(clockPin, HIGH);
    delayMicroseconds(40);
    digitalWrite(clockPin, LOW);
  }

  // Manual bit-bang Byte2
  for (int bit = 0; bit < 8; bit++) {
    digitalWrite(dataPin, (Byte2 >> bit) & 1);
    delayMicroseconds(40);
    digitalWrite(clockPin, HIGH);
    delayMicroseconds(40);
    digitalWrite(clockPin, LOW);
  }

  digitalWrite(latchPin, HIGH);
  delay(Delay);
}

void GPSSync() {
  // Show WiFi symbol while waiting for valid GPS
  Shift2Bytes(SegDisplay[1][12], SegDisplay[0][12], W);
  delay(10000);  // Give GPS time to acquire signal

  while (true) {
    String gps_data = gps_serial.readStringUntil('\n');
    Serial.println(gps_data);  // Debug heartbeat

    int S = gps_data.substring(7, 9).toInt();  // Seconds

    if (gps_data.startsWith("$GPGGA") && S != 0) {
      int hh = gps_data.substring(7, 9).toInt();
      int mm = gps_data.substring(9, 11).toInt();
      int ss = gps_data.substring(11, 13).toInt();

      hh += timezone;
      if (hh >= 24) hh -= 24;

      rtc.adjust(DateTime(gps.date.year(), gps.date.month(), gps.date.day(), hh, mm, ss));
      Serial.println("GPS time set!");
      break;
    }
  }
}

void setup() {
  Serial.begin(9600);
  gps_serial.begin(9600);
  Wire.begin();
  rtc.begin();

  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(dataPin,  OUTPUT);
  pinMode(TimeSync, INPUT_PULLUP);
  pinMode(Adjust,   INPUT_PULLUP);

  Serial.println("Nano ESP32 VFD Clock starting...");

  GPSSync();  // Sync RTC from GPS on boot
}


// Original Routine

void loop() {
  DateTime now = rtc.now();

  char timeString[6];
  sprintf(timeString, "%02d%02d", now.hour(), now.minute());

  for (int i = 0; i < 4; i++) {
    char x = timeString[i];
    byte b = x - '0';  // Digit 0-9

    Serial.println(b);  // Debug: see which digit is being shown

    // Animation: build up layers
    Shift2Bytes(SegDisplay[9][b], SegDisplay[8][b], W);
    Shift2Bytes(SegDisplay[7][b], SegDisplay[6][b], W);
    Shift2Bytes(SegDisplay[5][b], SegDisplay[4][b], W);
    Shift2Bytes(SegDisplay[3][b], SegDisplay[2][b], W);
    Shift2Bytes(SegDisplay[1][b], SegDisplay[0][b], 1000);  // Hold full digit

    // Fade out in reverse
    Shift2Bytes(SegDisplay[3][b], SegDisplay[2][b], W);
    Shift2Bytes(SegDisplay[5][b], SegDisplay[4][b], W);
    Shift2Bytes(SegDisplay[7][b], SegDisplay[6][b], W);
    Shift2Bytes(SegDisplay[9][b], SegDisplay[8][b], W);

    // Blank
    Shift2Bytes(SegDisplay[1][11], SegDisplay[0][11], 200);
  }

  delay(1600);  // Pause between full time displays
}




// Seconds Routine

// void loop() {
//   DateTime now = rtc.now();

//   // === Phase 1: Show HH:MM (4 digits) ===
//   char hhmm[5];
//   sprintf(hhmm, "%02d%02d", now.hour(), now.minute());

//   for (int i = 0; i < 4; i++) {
//     byte b = hhmm[i] - '0';

//     Serial.print(b);
//     if (i < 3) Serial.print(" ");

//     // Build up
//     Shift2Bytes(SegDisplay[9][b], SegDisplay[8][b], W);
//     Shift2Bytes(SegDisplay[7][b], SegDisplay[6][b], W);
//     Shift2Bytes(SegDisplay[5][b], SegDisplay[4][b], W);
//     Shift2Bytes(SegDisplay[3][b], SegDisplay[2][b], W);
//     Shift2Bytes(SegDisplay[1][b], SegDisplay[0][b], 800);  // Hold

//     // Fade out
//     Shift2Bytes(SegDisplay[3][b], SegDisplay[2][b], W);
//     Shift2Bytes(SegDisplay[5][b], SegDisplay[4][b], W);
//     Shift2Bytes(SegDisplay[7][b], SegDisplay[6][b], W);
//     Shift2Bytes(SegDisplay[9][b], SegDisplay[8][b], W);

//     // Blank
//     Shift2Bytes(SegDisplay[1][11], SegDisplay[0][11], 150);
//     delay(50);  // Inter-digit pause
//   }
//   Serial.println(" - HH:MM");

//   delay(200);  // Hold the full HH:MM a bit longer before switching

//   // === Phase 2: Show :SS (two digits, perhaps with colon if supported) ===
//   char ss[3];
//   sprintf(ss, "%02d", now.second());

//   // Optional: If your tube has a colon/decimal point segment, light it here first
//   // For now, just show the two digits sequentially
//   for (int i = 0; i < 2; i++) {
//     byte b = ss[i] - '0';

//     Serial.print(b);
//     if (i == 0) Serial.print(":");

//     // Same animation for seconds digits
//     Shift2Bytes(SegDisplay[9][b], SegDisplay[8][b], W);
//     Shift2Bytes(SegDisplay[7][b], SegDisplay[6][b], W);
//     Shift2Bytes(SegDisplay[5][b], SegDisplay[4][b], W);
//     Shift2Bytes(SegDisplay[3][b], SegDisplay[2][b], W);
//     Shift2Bytes(SegDisplay[1][b], SegDisplay[0][b], 100);  // Longer hold for seconds

//     // Fade out
//     Shift2Bytes(SegDisplay[3][b], SegDisplay[2][b], W);
//     Shift2Bytes(SegDisplay[5][b], SegDisplay[4][b], W);
//     Shift2Bytes(SegDisplay[7][b], SegDisplay[6][b], W);
//     Shift2Bytes(SegDisplay[9][b], SegDisplay[8][b], W);

//     // Blank
//     Shift2Bytes(SegDisplay[1][11], SegDisplay[0][11], 100);
//     delay(100);  // Slightly longer pause between SS digits
//   }
//   Serial.println(" - :SS");

//   delay(1000);  // Short pause before next cycle
// }



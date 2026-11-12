// Version 1.2 for NANO 04/02 Development
// Working with real GPS Sync
// Hard TZ Offset for now
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

// DO NOT EDIT
// DO NOT EDIT
// DO NOT EDIT

#include <Arduino.h>
#include <RTClib.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>
#include <SPI.h>
#include <Wire.h>

TinyGPSPlus gps; // The TinyGPS++ object
RTC_DS3231 rtc;

const int RX_PIN = 9, TX_PIN = 10; // GPS Pins
const int rtc_address = 0x68;// I2C address of RTC module

int TimeSync = 11; // Sets the timesync pin to 5
int Adjust = 12;
int timezone = 10; // timezone offset
int W = 40; // Delay between each layer of the digit

int latchPin = 4; // connect to the ST_CP of 74HC595 (pin 3,latch pin)
int clockPin = 5; // connect to the SH_CP of 74HC595 (pin 4, clock pin)
int dataPin = 2; // connect to the DS of 74HC595 (pin 2, data pin)

SoftwareSerial gps_serial(RX_PIN, TX_PIN); // The serial interface to the GPS device
String gps_data; // I think i need this

// +++++++++++++ Segment definitions. Two bytes per digit. (16 segment digits)

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
{B10111011,B10111011,B10111011,B10111011,B11111111,B10111011,B10111011,B11111111,B10111011,B10111111,B00000000,B11111111,B11100011} // J Seg
};

// Function: "Shift2Bytes" that gets called 10 times in the main loop
void Shift2Bytes(byte Byte1, byte Byte2,int Delay)
{
    // 1 set the latchPin to low potential, before sending data
    digitalWrite(latchPin, LOW);
    shiftOut(dataPin, clockPin, LSBFIRST, Byte1);
    // Send First Half of Digit
    shiftOut(dataPin, clockPin, LSBFIRST, Byte2);
    // Send Second Half of Digit
    // set the latchPin to high potential, after sending data
    digitalWrite(latchPin, HIGH);
    // while (Serial.available() == 0) { };
    // Serial.read();
    // Serial.println("Key pressed! Exiting the loop.");
    delay(Delay);
}


 // Function GPSSync: Set RTC with GPS - Get NEMA String, make sure its valid data and extract hhmmss digits
void GPSSync(){
   // Send the WiFi Symbol to the Tube. This will stay until GPS string is valid
  Shift2Bytes(SegDisplay[1][12], SegDisplay[0][12], W);
  delay(10000); // Time for the GPS to settle down (300000 for 5 minutes)

  while (true) { 
    String gps_data = gps_serial.readStringUntil('\n');
    Serial.println(gps_data);                 // Serial Monitor Heatbeat Only
    int S = gps_data.substring(7, 9).toInt(); // Grabs the seconds string in advance and converts to Big S 
    if ((gps_data.startsWith("$GPGGA")) && (S != 0)) // Makes sure that $GPGGA is present and makes sure seconds is NOT zero

    { // If good above, do all this Now
      int hh = gps_data.substring(7, 9).toInt(); // Extract Hour
      int mm = gps_data.substring(9, 11).toInt(); // Extract Minutes
      int ss = gps_data.substring(11, 13).toInt(); // Extract Seconds (again but diff variable)
      hh = hh + timezone; // add the timezone offset. Need to automate this.
      if (hh >= 24) hh = hh - 24; // Normalise so we dont get 33 o'clock for example
      rtc.adjust(DateTime(gps.date.year(), gps.date.month(), gps.date.day(), hh, mm, ss)); // Need to also get Date info for auto DLS
      break; 
    } // End get time set time
  } // End check GPS for valid time loop
}

void setup() {
  Serial.begin(9600);
  gps_serial.begin(9600);
  Wire.begin(); // Dont think i need this now
  rtc.begin(); 
  
// rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Only use if GPS breaks

// Define the Pin Funtions
  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(dataPin, OUTPUT);
  pinMode(TimeSync, INPUT_PULLUP); // Holds Pin 11 High waiting for ground contact
  pinMode(Adjust, INPUT_PULLUP); // Holds Pin 12 High waiting for ground contact

  GPSSync(); // Sets the RTC via GPS on restart

} // End Setup


void loop() {

  DateTime now = rtc.now();

   char timeString[6]; // Convert the time and date values into strings
   sprintf(timeString, "%02d%02d", now.hour(), now.minute());
 
 
  // Select the 4 timestring array values one digit at a time. This is the engine that steps through the timestring elements above. 
  // It's working with strings so later on I convert string -> char -> byte

    for (int i = 0; i < 4; i++) // Loops 1 to 4 for each digit HHMM
     { //x 
      String the_number = String(timeString[i]); // looks at each of the 4 char/numbers in timestring and allocates them in turn to "the number"

      { // y
     Serial.println(the_number[0]); // Should see the number digit displayed in IDE serial monitor
     char x = the_number[0];     // convert the number[0] to a character variable named x
     byte b = x - '0';    // convert x to a byte value called b for use below

// THIS SECTION Lights and unlights the Digit a layer at a time

    Shift2Bytes(SegDisplay[9][b], SegDisplay[8][b], W);
    Shift2Bytes(SegDisplay[7][b], SegDisplay[6][b], W);
    Shift2Bytes(SegDisplay[5][b], SegDisplay[4][b], W);
    Shift2Bytes(SegDisplay[3][b], SegDisplay[2][b], W);
    Shift2Bytes(SegDisplay[1][b], SegDisplay[0][b], 1000);
    Shift2Bytes(SegDisplay[3][b], SegDisplay[2][b], W);
    Shift2Bytes(SegDisplay[5][b], SegDisplay[4][b], W);
    Shift2Bytes(SegDisplay[7][b], SegDisplay[6][b], W);
    Shift2Bytes(SegDisplay[9][b], SegDisplay[8][b], W);
    Shift2Bytes(SegDisplay[1][11], SegDisplay[0][11], 200); // Blank Display

       } // End of individual digit 
 
    } // End of block of 4 digits
        
    delay(1600); // 1500 milliseconds blank time between each time display block is about right
    
   } // END. Back toi start to get new time



// Code Bits and Pieces

  // Send the WiFi Symbol to the Tube. This will stay until GPS string is valid
    //  digitalWrite(latchPin, LOW);
    //  shiftOut(dataPin, clockPin, LSBFIRST, B_seg_digits[12]);  // Change number to 10 for All on
    //  shiftOut(dataPin, clockPin, LSBFIRST, A_seg_digits[12]);  // Change number to 10 for All on
    //  // set the latchPin to high potential, after sending data
    //  digitalWrite(latchPin, HIGH);
  
      // Shift2Bytes(J_seg_digits[b], I_seg_digits[b], W);
      // Shift2Bytes(H_seg_digits[b], G_seg_digits[b], W);
      // Shift2Bytes(F_seg_digits[b], E_seg_digits[b], W);
      // Shift2Bytes(D_seg_digits[b], C_seg_digits[b], W);
      // Shift2Bytes(B_seg_digits[b], A_seg_digits[b], 1000);
      // Shift2Bytes(D_seg_digits[b], C_seg_digits[b], W);
      // Shift2Bytes(F_seg_digits[b], E_seg_digits[b], W);
      // Shift2Bytes(H_seg_digits[b], G_seg_digits[b], W);
      // Shift2Bytes(J_seg_digits[b], I_seg_digits[b], W);
       //Shift2Bytes(B_seg_digits[11], A_seg_digits[11], 200); // Blank Digit

// byte A_seg_digits[13] = {

// B00101110, // = 0
// B11110110, // = 1
// B00011110, // = 2
// B10001110, // = 3
// B01011101, // = 4
// B10011100, // = 5
// B11011100, // = 6
// B10101110, // = 7
// B00011100, // = 8
// B00011100, // = 9
// B00000000, // = All On
// B11111111, // = Blank
// B11100011  // - WiFi Symbol
// };

// byte B_seg_digits[13] = {

// B00110000, // = 0
// B10101011, // = 1
// B10110011, // = 2
// B00111011, // = 3
// B01111110, // = 4
// B00111010, // = 5
// B00111000, // = 6
// B11101111, // = 7
// B00111000, // = 8
// B00111110, // = 9
// B00000000, // = All On
// B11111111, // = Blank
// B11101111  // = WiFi Symbol
// };


// // C & D levels 4/5 lit

// byte C_seg_digits[10] = {

// B01101111, // = 0
// B11110111, // = 1
// B01011111, // = 2
// B11001111, // = 3
// B01011101, // = 4
// B11011101, // = 5
// B11011101, // = 6
// B11101111, // = 7
// B01011101, // = 8
// B01011101, // = 9
// };


// byte D_seg_digits[10] = {

// B00110000, // = 0
// B10101011, // = 1
// B10110011, // = 2
// B00111011, // = 3
// B01111110, // = 4
// B00111010, // = 5
// B00111000, // = 6
// B11101111, // = 7
// B00111000, // = 8
// B00111110, // = 9
// };


// // E & F levels 3/5 lit

// byte E_seg_digits[10] = {

// B11111111, // = 0
// B11111111, // = 1
// B11011111, // = 2
// B11011111, // = 3
// B11011101, // = 4
// B11011101, // = 5
// B11011101, // = 6
// B11111111, // = 7
// B11011101, // = 8
// B11011101, // = 9
// };


// byte F_seg_digits[10] = {

// B00110001, // = 0
// B10101011, // = 1
// B10110011, // = 2
// B00111011, // = 3
// B01111111, // = 4
// B00111011, // = 5
// B00111001, // = 6
// B11101111, // = 7
// B00111001, // = 8
// B00111111, // = 9
// };



// // G & H levels 2/5 lit

// byte G_seg_digits[10] = {

// B11111111, // = 0
// B11111111, // = 1
// B11111111, // = 2
// B11111111, // = 3
// B11111111, // = 4
// B11111111, // = 5
// B11111111, // = 6
// B11111111, // = 7
// B11111111, // = 8
// B11111111, // = 9
// };


// byte H_seg_digits[10] = {

// B00110001, // = 0
// B10101011, // = 1
// B10110011, // = 2
// B00111011, // = 3
// B01111111, // = 4
// B00111011, // = 5
// B00111001, // = 6
// B11101111, // = 7
// B00111001, // = 8
// B00111111, // = 9
// };


// // Level 1 I & J 1/5 lit

// byte I_seg_digits[10] = {

// B11111111, // = 0
// B11111111, // = 1
// B11111111, // = 2
// B11111111, // = 3
// B11111111, // = 4
// B11111111, // = 5
// B11111111, // = 6
// B11111111, // = 7
// B11111111, // = 8
// B11111111, // = 9
// };


// byte J_seg_digits[10] = {

// B10111011, // = 0
// B10111011, // = 1
// B10111011, // = 2
// B10111011, // = 3
// B11111111, // = 4
// B10111011, // = 5
// B10111011, // = 6
// B11111111, // = 7
// B10111011, // = 8
// B10111111, // = 9
// };

// +++++++++++++++++++++++++++

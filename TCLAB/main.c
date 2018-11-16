#include <atmel_start.h>






/*
  TCLab Temperature Control Lab Firmware
  Jeffrey Kantor
  January, 2018

  This firmware provides a high level interface to the Temperature Control Lab. The
  firmware scans the serial port for case-insensitive commands. Each command returns
  a result string.

  A         software restart. Returns "Start".
  LED float set LED to float for 10 sec. range 0 to 100. Returns actual float
  P1 float  set pwm limit on heater 1, range 0 to 255. Default 200. Returns P1.
  P2 float  set pwm limit on heater 2, range 0 to 255. Default 100. Returns P2.
  Q1 float  set Heater 1, range 0 to 100. Returns value of Q1.
  Q2 float  set Heater 2, range 0 to 100. Returns value of Q2.
  R1        get value of Heater 1, range 0 to 100
  R2        get value of Heater 2, range 0 to 100
  SCAN      get values T1 T2 Q1 Q1 in line delimited values
  T1        get Temperature T1. Returns value of T1 in �C.
  T2        get Temperature T2. Returns value of T2 in �C.
  VER       get firmware version string
  X         stop, enter sleep mode. Returns "Stop".

  Limits on the heater can be configured with the constants below.

  Status is indicated by LED1 on the Temperature Control Lab. Status conditions are:

      LED1        LED1
      Brightness  State
      ----------  -----
      dim         steady     Normal operation, heaters off
      bright      steady     Normal operation, heaters on
      dim         blinking   High temperature alarm on, heaters off
      bright      blinking   High temperature alarm on, heaters on

  The Temperature Control Lab shuts down the heaters if it receives no host commands
  during a timeout period (configure below), receives an "X" command, or receives
  an unrecognized command from the host.

  The constants can be used to configure the firmware.

  Version History
      1.0.1 first version included in the tclab package
      1.1.0 added R1 and R2 commands to read current heater values
            modified heater values to units of percent of full power
            added P1 and P2 commands to set heater power limits
            rewrote readCommand to avoid busy states
            simplified LED status model
      1.2.0 added LED command
      1.2.1 correctly reset heater values on close
            added version history
      1.2.2 shorten version string for better display by TCLab
      1.2.3 move baudrate to from 9600 to 115200
      1.3.0 add SCAN function
            report board type in version string
      1.4.0 changed Q1 and Q2 to float from int
*/

// determine board type
#if ARDUINO >= 100
  #include "Arduino.h"
#endif

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
  String boardType = "Arduino Uno";
  #define wSerial Serial
#elif defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega16U4__)
  String boardType = "Arduino Leonardo/Micro";
  #include <WebUSB.h>
  WebUSB WebUSBSerial(1, "webusb.github.io/arduino/demos/console/");
  #define wSerial WebUSBSerial
#elif defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
  String boardType = "Arduino Mega";
  #define wSerial ""
#else 
  String boardType = "Unknown board";
#endif

#include <Arduino.h>
#include "Linduino.h"
#include "LT_SPI.h"


#include "LTC2984_configuration_constants.h"
#include "LTC2984_support_functions.h"
#include "LTC2984_table_coeffs.h"
#define CHIP_SELECT QUIKEVAL_CS

// Enable debugging output
const bool DEBUG = false;

// constants
const String vers = "1.4.0";   // version of this firmware
const long baud = 115200;      // serial baud rate
const char sp = ' ';           // command separator
const char nl = '\n';          // command terminator

// pin numbers corresponding to signals on the TC Lab Shield
const int pinT1   = 0;         // T1
const int pinT2   = 2;         // T2
const int pinQ1   = 3;         // Q1
const int pinQ2   = 5;         // Q2
const int pinLED1 = 9;         // LED1

// temperature alarm limits
const int limT1   = 50;       // T1 high alarm (�C)
const int limT2   = 50;       // T2 high alarm (�C)

// LED1 levels
const int hiLED   =  60;       // hi LED
const int loLED   = hiLED/16;  // lo LED

// global variables
char Buffer[64];               // buffer for parsing serial input
int buffer_index = 0;          // index for Buffer
String cmd;                    // command
float val;                       // command value
int ledStatus;                 // 1: loLED
                               // 2: hiLED
                               // 3: loLED blink
                               // 4: hiLED blink
long ledTimeout = 0;           // when to return LED to normal operation
float LED = 100;               // LED override brightness
float P1 = 200;                // heater 1 power limit in units of pwm. Range 0 to 255
float P2 = 100;                // heater 2 power limit in units in pwm, range 0 to 255
float Q1 = 0;                  // last value written to heater 1 in units of percent
float Q2 = 0;                  // last value written to heater 2 in units of percent
int alarmStatus;               // hi temperature alarm status
bool newData = false;       // boolean flag indicating new command
bool webusb = false;        // boolean flag to select local or WebUSB interface



void readCommand() {
  if (!webusb) {
    while (Serial && (Serial.available() > 0) && (newData == false)) {
      int byte = Serial.read();
      if ((byte != '\r') && (byte != nl) && (buffer_index < 64)) {
        Buffer[buffer_index] = byte;
        buffer_index++;
      }
      else {
        newData = true;
      }
    }
  }
}

// for debugging with the serial monitor in Arduino IDE
void echoCommand() {
  if (newData) {
    Serial.write("Received Command: ");
    Serial.write(Buffer, buffer_index);
    Serial.write(nl);
    Serial.flush();
  }
}

// return thermister temperature in �C
inline float readTemperature(int pin) {
  //return analogRead(pin) * 0.3223 - 50.0;
  return measure_channel(CHIP_SELECT, 4, TEMPERATURE);
}

void parseCommand(void) {
  if (newData) {
    String read_ = String(Buffer);

    // separate command from associated data
    int idx = read_.indexOf(sp);
    cmd = read_.substring(0, idx);
    cmd.trim();
    cmd.toUpperCase();

    // extract data. toFloat() returns 0 on error
    String data = read_.substring(idx + 1);
    data.trim();
    val = data.toFloat();

    // reset parameter for next command
    memset(Buffer, 0, sizeof(Buffer));
    buffer_index = 0;
    newData = false;
  }
}

void sendResponse(String msg) {
  if (!webusb)
    Serial.println(msg);
  //else
  //  wSerial.println(msg);
}

void dispatchCommand(void) {
  if (cmd == "A") {
    setHeater1(0);
    setHeater2(0);
    sendResponse("Start");
  }
  else if (cmd == "LED") {
    ledTimeout = millis() + 10000;
    LED = max(0, min(100, val));
    sendResponse(String(LED));
  }
  else if (cmd == "P1") {
    P1 = max(0, min(255, val));
    sendResponse(String(P1));
  }
  else if (cmd == "P2") {
    P2 = max(0, min(255, val));
    sendResponse(String(P2));
  }
  else if (cmd == "Q1") {
    setHeater1(val);
    sendResponse(String(Q1));
  }
  else if (cmd == "Q2") {
    setHeater2(val);
    sendResponse(String(Q2));
  }
  else if (cmd == "R1") {
    sendResponse(String(Q1));
  }
  else if (cmd == "R2") {
    sendResponse(String(Q2));
  }
  else if (cmd == "SCAN") {
    sendResponse(String(readTemperature(pinT1)));
    sendResponse(String(readTemperature(pinT2)));
    sendResponse(String(Q1));
    sendResponse(String(Q2));
  }
  else if (cmd == "T1") {
    sendResponse(String(readTemperature(pinT1)));
  }
  else if (cmd == "T2") {
    sendResponse(String(readTemperature(pinT2)));
  }
  else if (cmd == "VER") {
    sendResponse("TCLab Firmware " + vers + " " + boardType);
  }
  else if ((cmd == "X") or (cmd.length() > 0)) {
    setHeater1(0);
    setHeater2(0);
    sendResponse(cmd);
  }
  if (!webusb)
    Serial.flush();
  else
  cmd = "";
}

void checkAlarm(void) {
  if ((readTemperature(pinT1) > limT1) or (readTemperature(pinT2) > limT2)) {
    alarmStatus = 1;
  }
  else {
    alarmStatus = 0;
  }
}

void updateStatus(void) {
  // determine led status
  ledStatus = 1;
  if ((Q1 > 0) or (Q2 > 0)) {
    ledStatus = 2;
  }
  if (alarmStatus > 0) {
    ledStatus += 2;
  }
  // update led depending on ledStatus
  if (millis() < ledTimeout) {        // override led operation
    analogWrite(pinLED1, LED);
  }
  else {
    switch (ledStatus) {
      case 1:  // normal operation, heaters off
        analogWrite(pinLED1, loLED);
        break;
      case 2:  // normal operation, heater on
        analogWrite(pinLED1, hiLED);
        break;
      case 3:  // high temperature alarm, heater off
        if ((millis() % 2000) > 1000) {
          analogWrite(pinLED1, loLED);
        } else {
          analogWrite(pinLED1, loLED/4);
        }
        break;
      case 4:  // high temperature alarm, heater on
        if ((millis() % 2000) > 1000) {
          analogWrite(pinLED1, hiLED);
        } else {
          analogWrite(pinLED1, loLED);
        }
        break;
    }   
  }
}

// set Heater 1
void setHeater1(float qval) {
  Q1 = max(0., min(qval, 100.));
  analogWrite(pinQ1, (Q1*P1)/100);
}

// set Heater 2
void setHeater2(float qval) {
  Q2 = max(0., min(qval, 100.));
  analogWrite(pinQ2, (Q2*P2)/100);
}

void configure_channels()
{
  uint8_t channel_number;
  uint32_t channel_assignment_data;

  // ----- Channel 2: Assign Sense Resistor -----
  channel_assignment_data = 
    SENSOR_TYPE__SENSE_RESISTOR |
    (uint32_t) 0x19000 << SENSE_RESISTOR_VALUE_LSB;		// sense resistor - value: 100.
  assign_channel(CHIP_SELECT, 2, channel_assignment_data);
  // ----- Channel 4: Assign Thermistor 44006 10K@25C -----
  channel_assignment_data = 
    SENSOR_TYPE__THERMISTOR_44006_10K_25C |
    THERMISTOR_RSENSE_CHANNEL__2 |
    THERMISTOR_DIFFERENTIAL |
    THERMISTOR_EXCITATION_MODE__SHARING_ROTATION |
    THERMISTOR_EXCITATION_CURRENT__AUTORANGE;
  assign_channel(CHIP_SELECT, 4, channel_assignment_data);

}




void configure_global_parameters() 
{
  // -- Set global parameters
  transfer_byte(CHIP_SELECT, WRITE_TO_RAM, 0xF0, TEMP_UNIT__C |
    REJECTION__50_60_HZ);
  // -- Set any extra delay between conversions (in this case, 0*100us)
  transfer_byte(CHIP_SELECT, WRITE_TO_RAM, 0xFF, 0);
}


// arduino startup
void setup() {
  //analogReference(EXTERNAL);
  while (!Serial) {
    ; // wait for serial port to connect.
  }
  Serial.begin(baud);
  Serial.flush();
  
  //if (wSerial) {
  //  wSerial.begin(baud);
  //  wSerial.flush();
  // }
  quikeval_SPI_init();          // Configure the spi port for 4MHz SCK
  quikeval_SPI_connect(); 
  pinMode(CHIP_SELECT, OUTPUT); 
  configure_channels();			
  configure_global_parameters();
  setHeater1(0);
  setHeater2(0);
  ledTimeout = millis() + 1000;
}

// arduino main event loop
void loop() {
  webusb = false;
  readCommand();
  if (DEBUG) echoCommand();
  parseCommand();
  dispatchCommand();
  checkAlarm();
  updateStatus();
}






int main(void)
{
	/* Initializes MCU, drivers and middleware */
	atmel_start_init();
	setup();
	/* Replace with your application code */
	while (1) {
		loop();
	}
}

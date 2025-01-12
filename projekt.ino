#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_TSL2591.h>
#include "Bonezegei_ULN2003_Stepper.h"
#include "BluetoothSerial.h"

#define MANUAL_TURN_ON 1
#define MANUAL_TURN_OFF 2
#define UPDATE_SETTINGS 3
#define MANUAL_SYNC_TIME 4

#define FORWARD 1
#define REVERSE 0

#define I2C_SDA 32
#define I2C_SCL 33

#define DS3231_I2C_ADDRESS 0x68
#define TSL2591_I2C_ADDRESS 0x29

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled
#endif

RTC_DS3231 rtc;
Adafruit_TSL2591 tsl = Adafruit_TSL2591(2591);
Bonezegei_ULN2003_Stepper Stepper(25, 26, 27, 14);
BluetoothSerial SerialBT;

void setup() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.begin(115200);
  Serial.println("esp32 project");

  if (!Stepper.begin()) {
    Serial.println("Stepper fail");
    while (1)
      ;
  }
  Stepper.setSpeed(5);

  // setDS3231time(30,42,16,5,13,10,16);
  if (!rtc.begin()) {
    Serial.println("RTC fail");
    while (1)
      ;
  }
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  if (!tsl.begin()) {
    Serial.println("TSL fail");
    while (1)
      ;
  }
  tsl.setGain(TSL2591_GAIN_MED);
  tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);

  if (!SerialBT.begin("ESP32test")) {
    Serial.println("Bluetooth fail");
    while (1)
      ;
  }
}

void loop() {
  if (Serial.available()) {
    SerialBT.write(Serial.read());
  }
  //if (SerialBT.available()) {
  // Serial.write(SerialBT.read());
  //  bluetooth_code = SerialBT.read();
  //}


  int bluetooth_code = 0;
  if (SerialBT.available()) {
    String received_string = "";  // Initialize an empty string

    while (SerialBT.available()) {
      char incoming_char = SerialBT.read();  // Read one character at a time
      received_string += incoming_char;      // Append it to the string
      delay(10);                             // Small delay to allow more data to arrive
    }

    received_string.trim();

    int bluetooth_code = received_string.toInt();

    Serial.print("Received Code as Integer: ");
    Serial.println(bluetooth_code);
  }

  // Serial.println(bluetooth_code);

  uint32_t lum = tsl.getFullLuminosity();
  uint16_t ir = lum >> 16;
  uint16_t full = lum & 0xFFFF;
  uint16_t visible = full - ir;
  float lux = tsl.calculateLux(full, ir);

  Serial.print("IR: ");
  Serial.println(ir);
  Serial.print("Full: ");
  Serial.println(full);
  Serial.print("Visible: ");
  Serial.println(visible);
  Serial.print("Lux: ");
  Serial.println(lux, 2);

  Serial.println();

  DateTime now = rtc.now();

  Serial.print("Date & Time: ");
  Serial.print(now.year(), DEC);
  Serial.print("-");
  Serial.print(now.month(), DEC);
  Serial.print("-");
  Serial.print(now.day(), DEC);
  Serial.print("\t");
  Serial.print(now.hour(), DEC);
  Serial.print(":");
  Serial.print(now.minute(), DEC);
  Serial.print(":");
  Serial.println(now.second(), DEC);

  Serial.println();

  if (lux > 100) {
    Stepper.step(FORWARD, 2000);
  }

  delay(1000);
}

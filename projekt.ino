// ArduinoIDE
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_TSL2591.h>
#include "Bonezegei_ULN2003_Stepper.h"
#include "BluetoothSerial.h"
#include <ArduinoJson.h>
#include <EEPROM.h>

// Constants
#define MANUAL_TURN_ON 1
#define MANUAL_TURN_OFF 2
#define UPDATE_SETTINGS 3
#define MANUAL_SYNC_TIME 4
#define REQUEST_STATUS 5
#define FORWARD 1
#define REVERSE 0
#define I2C_SDA 32
#define I2C_SCL 33
#define EEPROM_SIZE 512
#define SETTINGS_VERSION 1

// EEPROM addresses
#define ADDR_SETTINGS_VALID 0    // 1 byte
#define ADDR_OPEN_TIME 1         // 5 bytes (HH:MM)
#define ADDR_CLOSE_TIME 6        // 5 bytes (HH:MM)
#define ADDR_OPEN_LUX 11         // 6 bytes
#define ADDR_CLOSE_LUX 17        // 6 bytes
#define ADDR_OPEN_MODE 23        // 5 bytes (TIME/LIGHT)
#define ADDR_CLOSE_MODE 28       // 5 bytes (TIME/LIGHT)

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled
#endif

// Global objects
RTC_DS3231 rtc;
Adafruit_TSL2591 tsl = Adafruit_TSL2591(2591);
Bonezegei_ULN2003_Stepper Stepper(25, 26, 27, 14);
BluetoothSerial SerialBT;

// Settings structure
struct Settings {
    String openTime = "07:00";
    String closeTime = "20:00";
    int openLux = 50000;
    int closeLux = 10000;
    String openMode = "TIME";
    String closeMode = "TIME";
};

// Global variables
Settings settings;
unsigned long lastReadTime = 0;
unsigned long lastCheckTime = 0;
const unsigned long READ_INTERVAL = 1000;    // Sensor reading interval (1 second)
const unsigned long CHECK_INTERVAL = 60000;  // Schedule check interval (1 minute)
bool isProcessingCommand = false;
int currentCommand = 0;
bool blindsOpen = false;
float currentLux = 0;

// Function declarations
void loadSettings();
void saveSettings();
void checkSchedule();
void checkLightLevels();
void openBlinds();
void closeBlinds();
void sendStatus();

void setup() {
    // Initialize EEPROM
    EEPROM.begin(EEPROM_SIZE);
    
    // Initialize I2C and Serial communication
    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.begin(115200);
    Serial.println("ESP32 Smart Blinds Controller Starting...");

    // Initialize Stepper
    if (!Stepper.begin()) {
        Serial.println("Stepper initialization failed");
        while (1);
    }
    Stepper.setSpeed(5);

    // Initialize RTC
    if (!rtc.begin()) {
        Serial.println("RTC initialization failed");
        while (1);
    }

    // Initialize Light Sensor
    if (!tsl.begin()) {
        Serial.println("Light sensor initialization failed");
        while (1);
    }
    tsl.setGain(TSL2591_GAIN_MED);
    tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);

    // Initialize Bluetooth
    if (!SerialBT.begin("ESP32test")) {
        Serial.println("Bluetooth initialization failed");
        while (1);
    }

    // Load settings from EEPROM
    loadSettings();
    
    Serial.println("All systems initialized successfully");
}

void loadSettings() {
    // Check if valid settings exist
    if (EEPROM.read(ADDR_SETTINGS_VALID) == SETTINGS_VERSION) {
        char buffer[6];
        
        // Read openTime
        for (int i = 0; i < 5; i++) {
            buffer[i] = EEPROM.read(ADDR_OPEN_TIME + i);
        }
        buffer[5] = '\0';
        settings.openTime = String(buffer);

        // Read closeTime
        for (int i = 0; i < 5; i++) {
            buffer[i] = EEPROM.read(ADDR_CLOSE_TIME + i);
        }
        buffer[5] = '\0';
        settings.closeTime = String(buffer);

        // Read open/close lux values
        char luxBuffer[7];
        for (int i = 0; i < 6; i++) {
            luxBuffer[i] = EEPROM.read(ADDR_OPEN_LUX + i);
        }
        luxBuffer[6] = '\0';
        settings.openLux = atoi(luxBuffer);

        for (int i = 0; i < 6; i++) {
            luxBuffer[i] = EEPROM.read(ADDR_CLOSE_LUX + i);
        }
        luxBuffer[6] = '\0';
        settings.closeLux = atoi(luxBuffer);

        // Read modes
        char modeBuffer[6];
        for (int i = 0; i < 5; i++) {
            modeBuffer[i] = EEPROM.read(ADDR_OPEN_MODE + i);
        }
        modeBuffer[5] = '\0';
        settings.openMode = String(modeBuffer);

        for (int i = 0; i < 5; i++) {
            modeBuffer[i] = EEPROM.read(ADDR_CLOSE_MODE + i);
        }
        modeBuffer[5] = '\0';
        settings.closeMode = String(modeBuffer);
    }
}

void saveSettings() {
    // Mark settings as valid
    EEPROM.write(ADDR_SETTINGS_VALID, SETTINGS_VERSION);
    
    // Save openTime
    for (int i = 0; i < 5; i++) {
        EEPROM.write(ADDR_OPEN_TIME + i, settings.openTime[i]);
    }

    // Save closeTime
    for (int i = 0; i < 5; i++) {
        EEPROM.write(ADDR_CLOSE_TIME + i, settings.closeTime[i]);
    }

    // Save open/close lux values
    String openLuxStr = String(settings.openLux);
    String closeLuxStr = String(settings.closeLux);
    
    for (int i = 0; i < 6; i++) {
        EEPROM.write(ADDR_OPEN_LUX + i, openLuxStr[i]);
        EEPROM.write(ADDR_CLOSE_LUX + i, closeLuxStr[i]);
    }

    // Save modes
    for (int i = 0; i < 5; i++) {
        EEPROM.write(ADDR_OPEN_MODE + i, settings.openMode[i]);
        EEPROM.write(ADDR_CLOSE_MODE + i, settings.closeMode[i]);
    }

    EEPROM.commit();
}

void processCommand() {
    if (!isProcessingCommand) return;
    
    switch(currentCommand) {
        case MANUAL_TURN_ON:
            openBlinds();
            break;
            
        case MANUAL_TURN_OFF:
            closeBlinds();
            break;
            
        case REQUEST_STATUS:
            sendStatus();
            break;
    }
    
    isProcessingCommand = false;
    currentCommand = 0;
}

void handleBluetoothCommand(String command) {
    // Parse command
    if (command.startsWith(String(UPDATE_SETTINGS) + "|")) {
        // Handle settings update
        command.remove(0, 2); // Remove command code and separator
        
        // Split the command string into key-value pairs
        while (command.length() > 0) {
            int separatorIndex = command.indexOf("|");
            if (separatorIndex == -1) break;
            
            String pair = command.substring(0, separatorIndex);
            command = command.substring(separatorIndex + 1);
            
            int colonIndex = pair.indexOf(":");
            if (colonIndex == -1) continue;
            
            String key = pair.substring(0, colonIndex);
            String value = pair.substring(colonIndex + 1);
            
            if (key == "openTime") settings.openTime = value;
            else if (key == "closeTime") settings.closeTime = value;
            else if (key == "openLux") settings.openLux = value.toInt();
            else if (key == "closeLux") settings.closeLux = value.toInt();
            else if (key == "openMode") settings.openMode = value;
            else if (key == "closeMode") settings.closeMode = value;
        }
        
        saveSettings();
        SerialBT.println("OK:SETTINGS_UPDATED");
    }
    else if (command.startsWith(String(MANUAL_SYNC_TIME) + "|")) {
        // Handle time sync
        command.remove(0, 2); // Remove command code and separator
        
        int year = command.substring(0, 4).toInt();
        int month = command.substring(5, 7).toInt();
        int day = command.substring(8, 10).toInt();
        int hour = command.substring(11, 13).toInt();
        int minute = command.substring(14, 16).toInt();
        int second = command.substring(17, 19).toInt();
        
        rtc.adjust(DateTime(year, month, day, hour, minute, second));
        SerialBT.println("OK:TIME_SYNCED");
    }
    else {
        // Handle simple commands
        int code = command.toInt();
        switch(code) {
            case MANUAL_TURN_ON:
                SerialBT.println("OK:TURN_ON");
                break;
                
            case MANUAL_TURN_OFF:
                SerialBT.println("OK:TURN_OFF");
                break;
                
            case REQUEST_STATUS:
                sendStatus();
                break;
                
            default:
                SerialBT.println("ERROR:INVALID_COMMAND");
                return;
        }
        
        currentCommand = code;
        isProcessingCommand = true;
    }
}

void openBlinds() {
    if (!blindsOpen) {
        Stepper.step(FORWARD, 2000);
        blindsOpen = true;
    }
}

void closeBlinds() {
    if (blindsOpen) {
        Stepper.step(REVERSE, 2000);
        blindsOpen = false;
    }
}

void checkSchedule() {
    DateTime now = rtc.now();
    String currentTime = String(now.hour()) + ":" + String(now.minute());
    
    // Only check time-based triggers if the respective mode is set to "TIME"
    if (settings.openMode == "TIME" && currentTime == settings.openTime) {
        openBlinds();
    }
    if (settings.closeMode == "TIME" && currentTime == settings.closeTime) {
        closeBlinds();
    }
}

void checkLightLevels() {
    // Only check light-based triggers if the respective mode is set to "LIGHT"
    if (settings.openMode == "LIGHT" && !blindsOpen && currentLux >= settings.openLux) {
        openBlinds();
    }
    if (settings.closeMode == "LIGHT" && blindsOpen && currentLux <= settings.closeLux) {
        closeBlinds();
    }
}

void sendStatus() {
    DateTime now = rtc.now();
    StaticJsonDocument<200> doc;
    
    // Format time with leading zeros
    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", now.hour(), now.minute());
    
    doc["time"] = timeStr;
    doc["lux"] = currentLux;
    doc["blinds"] = blindsOpen ? "OPEN" : "CLOSED";
    
    String status;
    serializeJson(doc, status);
    SerialBT.println(status);
}

void updateSensorReadings() {
    uint32_t lum = tsl.getFullLuminosity();
    uint16_t ir = lum >> 16;
    uint16_t full = lum & 0xFFFF;
    currentLux = tsl.calculateLux(full, ir);
}

void loop() {
    // Handle Bluetooth communication
    if (SerialBT.available()) {
        String received_string = "";
        while (SerialBT.available()) {
            char incoming_char = SerialBT.read();
            received_string += incoming_char;
            delay(20);
        }
        received_string.trim();
        
        Serial.print("Received Bluetooth Command: ");
        Serial.println(received_string);
        
        handleBluetoothCommand(received_string);
    }

    // Process any pending commands
    processCommand();

    // Update sensor readings at regular intervals
    unsigned long currentTime = millis();
    if (currentTime - lastReadTime >= READ_INTERVAL) {
        lastReadTime = currentTime;
        updateSensorReadings();

        // Check light levels more frequently (every second with sensor readings)
        // This ensures responsive light-based operation
        if (settings.openMode == "LIGHT" || settings.closeMode == "LIGHT") {
            checkLightLevels();
        }
    }

    // Check schedule less frequently
    if (currentTime - lastCheckTime >= CHECK_INTERVAL) {
        lastCheckTime = currentTime;
        // Only check schedule if either open or close mode is set to TIME
        if (settings.openMode == "TIME" || settings.closeMode == "TIME") {
            checkSchedule();
        }
    }
}
#include <Arduino.h>
#include <Seeed_HM330X.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Wire.h>
#undef min
#undef max
#include <rpcBLEDevice.h>
#include <BLE2902.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <BLECharacteristic.h>

// Custom UUIDs for BLE service and characteristics

// Service UUID
#define SERVICE_UUID        "91bad492-b950-4226-aa2b-4ede9fa42f59"

// Characteristic UUIDs
#define PM1_0_CHAR_UUID    "91bad493-b950-4226-aa2b-4ede9fa42f59"
#define PM2_5_CHAR_UUID    "91bad494-b950-4226-aa2b-4ede9fa42f59"
#define PM10_CHAR_UUID     "91bad495-b950-4226-aa2b-4ede9fa42f59"

// Initialize TFT display
TFT_eSPI tft;

// Initialize PM2.5 sensor
HM330X sensor;
uint8_t buf[30];

// BLE objects
BLEServer* pServer = nullptr;
BLEService* pService = nullptr;
BLECharacteristic* pPM1_0Characteristic = nullptr;
BLECharacteristic* pPM2_5Characteristic = nullptr;
BLECharacteristic* pPM10Characteristic = nullptr;
bool deviceConnected = false;

// Structure to hold sensor readings
// std: standard industrial metal particles
// atm: atmospheric indoor/outdoor particles
struct PMReadings {
    uint16_t pm1_0_std;
    uint16_t pm2_5_std;
    uint16_t pm10_std;
    uint16_t pm1_0_atm;
    uint16_t pm2_5_atm;
    uint16_t pm10_atm;
};

PMReadings readings;

void displayDebugInfo(const char* message) {
    Serial.println(message);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    static int debugY = 0;
    if (debugY > 200) {
        tft.fillScreen(TFT_BLACK);
        debugY = 0;
    }
    tft.drawString(message, 10, debugY);
    debugY += 20;
}

// BLE server callbacks
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        displayDebugInfo("BLE Client Connected");
    };

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        displayDebugInfo("BLE Client Disconnected");
        // Restart advertising when disconnected
        pServer->getAdvertising()->start();
    }
};

void updateReadings(uint8_t* data) {
    // Print raw data for debugging
    Serial.println("Raw sensor data:");
    for (int i = 0; i < 29; i++) {
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    readings.pm1_0_std = (uint16_t)data[4] << 8 | data[5];
    readings.pm2_5_std = (uint16_t)data[6] << 8 | data[7];
    readings.pm10_std = (uint16_t)data[8] << 8 | data[9];
    readings.pm1_0_atm = (uint16_t)data[10] << 8 | data[11];
    readings.pm2_5_atm = (uint16_t)data[12] << 8 | data[13];
    readings.pm10_atm = (uint16_t)data[14] << 8 | data[15];

    // Update BLE characteristics if connected
    if (deviceConnected) {
        pPM1_0Characteristic->setValue(readings.pm1_0_atm);
        pPM2_5Characteristic->setValue(readings.pm2_5_atm);
        pPM10Characteristic->setValue(readings.pm10_atm);
        
        pPM1_0Characteristic->notify();
        pPM2_5Characteristic->notify();
        pPM10Characteristic->notify();
    }

    // Print parsed values
    Serial.println("Parsed values:");
    Serial.print("PM1.0 (std): "); Serial.println(readings.pm1_0_atm);
    Serial.print("PM2.5 (std): "); Serial.println(readings.pm2_5_atm);
    Serial.print("PM10 (std): "); Serial.println(readings.pm10_atm);
}

void drawReadings() {
    tft.fillScreen(TFT_BLACK);
    
    // Draw title
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("PM2.5 Sensor Readings", 10, 10);
    
    // Show BLE status
    tft.setTextColor(deviceConnected ? TFT_GREEN : TFT_RED);
    tft.drawString(deviceConnected ? "BLE: Connected" : "BLE: Waiting...", 10, 40);
    
    char buffer[50];
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    
    // Atmospheric measurements
    snprintf(buffer, sizeof(buffer), "PM1.0: %d ug/m3", readings.pm1_0_atm);
    tft.drawString(buffer, 10, 80);

    snprintf(buffer, sizeof(buffer), "PM2.5: %d ug/m3", readings.pm2_5_atm);
    tft.drawString(buffer, 10, 110);

    snprintf(buffer, sizeof(buffer), "PM10:  %d ug/m3", readings.pm10_atm);
    tft.drawString(buffer, 10, 140);
}

void setup() {
    // Initialize serial communication
    Serial.begin(115200);
    delay(100);
    displayDebugInfo("Starting setup...");

    // Initialize I2C
    Wire.begin();
    delay(100);
    displayDebugInfo("I2C initialized");

    // Initialize display
    tft.begin();
    tft.setRotation(3); // Landscape mode
    tft.fillScreen(TFT_BLACK);
    displayDebugInfo("Display initialized");

    // Initialize BLE
    displayDebugInfo("Initializing BLE...");
    BLEDevice::init("PM2.5 Sensor");
    
    // Create BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    // Create BLE Service
    pService = pServer->createService(SERVICE_UUID);

    // Create BLE Characteristics
    pPM1_0Characteristic = pService->createCharacteristic(
        PM1_0_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pPM1_0Characteristic->addDescriptor(new BLE2902());
  
    pPM2_5Characteristic = pService->createCharacteristic(
        PM2_5_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pPM2_5Characteristic->addDescriptor(new BLE2902());

    pPM10Characteristic = pService->createCharacteristic(
        PM10_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pPM10Characteristic->addDescriptor(new BLE2902());

    // Start the service
    pService->start();

    // Start advertising
    pServer->getAdvertising()->start();
    displayDebugInfo("BLE initialized");

    // Scan I2C devices
    displayDebugInfo("Scanning I2C devices...");
    byte error, address;
    int nDevices = 0;
    
    for(address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        
        if (error == 0) {
            char msg[32];
            snprintf(msg, sizeof(msg), "Device at: 0x%02X", address);
            displayDebugInfo(msg);
            nDevices++;
        }
    }
    
    if (nDevices == 0) {
        displayDebugInfo("No I2C devices found!");
    }
    
    // Initialize sensor
    displayDebugInfo("Initializing sensor...");
    if (sensor.init()) {
        displayDebugInfo("HM330X init failed!");
        Serial.println("HM330X init failed!!");
        while (1) {
            delay(100);
        }
    }
    displayDebugInfo("Sensor initialized successfully!");

    delay(1000); // Give some time to read the debug messages
    tft.fillScreen(TFT_BLACK);
}

void loop() {
    static unsigned long lastUpdate = 0;
    static int readCount = 0;
    
    // Print a heartbeat message every few seconds
    if (millis() - lastUpdate >= 5000) {
        readCount++;
        char msg[32];
        snprintf(msg, sizeof(msg), "Reading sensor... #%d", readCount);
        displayDebugInfo(msg);
        
        // Read sensor data
        if (sensor.read_sensor_value(buf, 29)) {
            displayDebugInfo("Sensor read failed!");
            return;
        }
        
        // Update and display readings
        updateReadings(buf);
        drawReadings();
        
        lastUpdate = millis();
    }
}
#include <Arduino.h>
#include <Seeed_HM330X.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <Wire.h>
#include "Seeed_Arduino_FS.h"
#include "SD/Seeed_SD.h"
#undef min
#undef max
#include <rpcBLEDevice.h>
#include <BLE2902.h>
#include <BLEServer.h>
#include <BLEService.h>
#include <BLECharacteristic.h>
#include "RTC_SAMD51.h"
#include "DateTime.h"
#include <vector>

// Pin definitions
#define LCD_BACKLIGHT (72UL) // LCD backlight control pin

// BLE Device name
#define DEVICE_NAME "PM2.5 Sensor"

// Custom UUIDs for BLE service and characteristics
#define SERVICE_UUID "91bad492-b950-4226-aa2b-4ede9fa42f59"
#define PM1_0_CHAR_UUID "91bad493-b950-4226-aa2b-4ede9fa42f59"
#define PM2_5_CHAR_UUID "91bad494-b950-4226-aa2b-4ede9fa42f59"
#define PM10_CHAR_UUID "91bad495-b950-4226-aa2b-4ede9fa42f59"
#define HISTORY_CHAR_UUID "91bad496-b950-4226-aa2b-4ede9fa42f59"
#define TIME_SYNC_CHAR_UUID "91bad497-b950-4226-aa2b-4ede9fa42f59"

// Logging interval (in milliseconds)
#define LOG_INTERVAL 300000 // 5 minutes

// Initialize objects
TFT_eSPI tft;
HM330X sensor;
uint8_t buf[30];
RTC_SAMD51 rtc;

// BLE objects
BLEServer *pServer = nullptr;
BLEService *pService = nullptr;
BLECharacteristic *pPM1_0Characteristic = nullptr;
BLECharacteristic *pPM2_5Characteristic = nullptr;
BLECharacteristic *pPM10Characteristic = nullptr;
BLECharacteristic *pHistoryCharacteristic = nullptr;
BLECharacteristic *pTimeSyncCharacteristic = nullptr;
bool deviceConnected = false;

void displayDebugInfo(const char *message)
{
    Serial.println(message);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    static int debugY = 0;
    if (debugY > 200)
    {
        tft.fillScreen(TFT_BLACK);
        debugY = 0;
    }
    tft.drawString(message, 10, debugY);
    debugY += 20;
}

// BLE server callbacks
class MyServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *pServer)
    {
        deviceConnected = true;
        displayDebugInfo("BLE Client Connected");
    };

    void onDisconnect(BLEServer *pServer)
    {
        deviceConnected = false;
        displayDebugInfo("BLE Client Disconnected");
        pServer->getAdvertising()->start();
    }
};

// Callback class for handling time sync
class TimeSyncCallback : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pCharacteristic)
    {
        std::string value = pCharacteristic->getValue();

        if (value.length() == 8)
        {
            // char rawBytes[50];
            // snprintf(rawBytes, sizeof(rawBytes), "Raw bytes: %02X %02X %02X %02X %02X %02X %02X %02X",
            //         (uint8_t)value[0], (uint8_t)value[1], (uint8_t)value[2], (uint8_t)value[3],
            //         (uint8_t)value[4], (uint8_t)value[5], (uint8_t)value[6], (uint8_t)value[7]);
            // displayDebugInfo(rawBytes);

            uint32_t newTime =
                ((uint32_t)(uint8_t)value[3] << 24) |
                ((uint32_t)(uint8_t)value[2] << 16) |
                ((uint32_t)(uint8_t)value[1] << 8) |
                ((uint32_t)(uint8_t)value[0]);

            // char timeValue[50];
            // snprintf(timeValue, sizeof(timeValue), "Timestamp: %lu", newTime);
            // displayDebugInfo(timeValue);

            DateTime newDateTime(newTime);
            rtc.adjust(newDateTime);

            char timeStr[32];
            sprintf(timeStr, "Time set to: %04d-%02d-%02d %02d:%02d:%02d",
                    newDateTime.year(), newDateTime.month(), newDateTime.day(),
                    newDateTime.hour(), newDateTime.minute(), newDateTime.second());
            displayDebugInfo(timeStr);
        }
    }
};

// Structure to hold sensor readings
struct PMReadings
{
    uint16_t pm1_0_std;
    uint16_t pm2_5_std;
    uint16_t pm10_std;
    uint16_t pm1_0_atm;
    uint16_t pm2_5_atm;
    uint16_t pm10_atm;
    time_t timestamp;
};

PMReadings readings;
std::vector<PMReadings> hourlyReadings;

// Function to update sensor readings
void updateReadings(uint8_t *data)
{
    readings.pm1_0_std = (uint16_t)data[4] << 8 | data[5];
    readings.pm2_5_std = (uint16_t)data[6] << 8 | data[7];
    readings.pm10_std = (uint16_t)data[8] << 8 | data[9];
    readings.pm1_0_atm = (uint16_t)data[10] << 8 | data[11];
    readings.pm2_5_atm = (uint16_t)data[12] << 8 | data[13];
    readings.pm10_atm = (uint16_t)data[14] << 8 | data[15];
    readings.timestamp = rtc.now().unixtime(); // Use RTC timestamp

    // Update BLE characteristics if connected
    if (deviceConnected)
    {
        pPM1_0Characteristic->setValue(readings.pm1_0_atm);
        pPM2_5Characteristic->setValue(readings.pm2_5_atm);
        pPM10Characteristic->setValue(readings.pm10_atm);

        pPM1_0Characteristic->notify();
        pPM2_5Characteristic->notify();
        pPM10Characteristic->notify();
    }
}

// Function to draw readings on display
void drawReadings()
{
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

// Function to get current date/time string with power cycle number
String getDateTimeString()
{
    static uint16_t powerCycle = 0;
    static bool initialized = false;

    if (!initialized)
    {
        if (SD.exists("/data/power_cycle.txt"))
        {
            File cycleFile = SD.open("/data/power_cycle.txt", FILE_READ);
            if (cycleFile)
            {
                powerCycle = cycleFile.parseInt();
                cycleFile.close();
            }
        }
        powerCycle++;
        File cycleFile = SD.open("/data/power_cycle.txt", FILE_WRITE);
        if (cycleFile)
        {
            cycleFile.println(powerCycle);
            cycleFile.close();
        }
        initialized = true;
    }

    DateTime now = rtc.now();
    char datetime[32];
    sprintf(datetime, "P%03d_%04d%02d%02d_%02d%02d%02d",
            powerCycle,
            now.year(),
            now.month(),
            now.day(),
            now.hour(),
            now.minute(),
            now.second());
    return String(datetime);
}

// Function to create new log file name
String getCurrentLogFile()
{
    static uint16_t fileCounter = 0;
    static bool initialized = false;

    if (!initialized)
    {
        // Find the highest existing file number
        for (uint16_t i = 0; i < 9999; i++)
        {
            char filename[32];
            sprintf(filename, "/data/PM_%04d.csv", i);
            if (!SD.exists(filename))
            {
                fileCounter = i;
                break;
            }
        }
        initialized = true;
    }

    char filename[32];
    sprintf(filename, "/PM_%04d.csv", fileCounter);
    return String(filename);
}

// Function to ensure the data directory exists
void ensureDataDirectory()
{
    if (!SD.exists("/data"))
    {
        SD.mkdir("/data");
    }
}

// Function to log data to SD card
void logToSD(const PMReadings &data)
{
    if (!SD.exists("/data"))
    {
        if (!SD.mkdir("/data"))
        {
            displayDebugInfo("Failed to create data directory!");
            return;
        }
    }

    String filename = "/data" + getCurrentLogFile();
    bool isNewFile = !SD.exists(filename);

    File dataFile = SD.open(filename, FILE_APPEND);
    if (!dataFile)
    {
        displayDebugInfo("Error opening log file!");
        return;
    }

    // Add header if new file
    if (isNewFile)
    {
        dataFile.println("Power_Cycle,Timestamp,PM1.0,PM2.5,PM10,Uptime_Sec");
    }

    // Write data with uptime
    String dataString = getDateTimeString() + "," +
                        String(data.pm1_0_atm) + "," +
                        String(data.pm2_5_atm) + "," +
                        String(data.pm10_atm) + "," +
                        String(millis() / 1000);

    if (dataFile.println(dataString))
    {
        displayDebugInfo("Data logged");
        Serial.println(dataString); // Echo to serial for debugging
    }
    else
    {
        displayDebugInfo("Write failed!");
    }

    dataFile.close();
}

// Function to calculate hourly averages
void calculateHourlyAverage()
{
    String filename = "/data" + getCurrentLogFile();
    File dataFile = SD.open(filename, FILE_READ);

    if (!dataFile)
    {
        displayDebugInfo("Failed to open data file");
        return;
    }

    uint32_t pm1_0_sum = 0;
    uint32_t pm2_5_sum = 0;
    uint32_t pm10_sum = 0;
    int count = 0;

    // Skip header line
    String header = dataFile.readStringUntil('\n');

    char debugMsg[100];
    snprintf(debugMsg, sizeof(debugMsg), "Processing file: %s", filename.c_str());
    displayDebugInfo(debugMsg);

    // Read and process each line
    while (dataFile.available())
    {
        String line = dataFile.readStringUntil('\n');
        if (line.length() == 0)
            continue;

        // Debug raw line
        snprintf(debugMsg, sizeof(debugMsg), "Raw line: %s", line.c_str());
        displayDebugInfo(debugMsg);

        // Count commas for validation
        int commaCount = 0;
        for (char c : line)
        {
            if (c == ',')
                commaCount++;
        }

        snprintf(debugMsg, sizeof(debugMsg), "Found %d commas in line", commaCount);
        displayDebugInfo(debugMsg);

        if (commaCount < 4)
        {
            displayDebugInfo("Not enough commas, skipping line");
            continue;
        }

        // Split the line into fields
        int start = 0;
        int fieldCount = 0;
        String fields[6]; // To store each field

        for (int i = 0; i < line.length(); i++)
        {
            if (line.charAt(i) == ',' || i == line.length() - 1)
            {
                if (i == line.length() - 1)
                    i++; // Include last character
                fields[fieldCount] = line.substring(start, i);
                fieldCount++;
                start = i + 1;
            }
        }

        // Extract PM values (fields 1, 2, and 3)
        int pm1_0 = fields[1].toInt();
        int pm2_5 = fields[2].toInt();
        int pm10 = fields[3].toInt();

        // Debug extracted values
        snprintf(debugMsg, sizeof(debugMsg), "Fields: [%s] [%s] [%s] [%s] [%s]",
                 fields[0].c_str(), fields[1].c_str(), fields[2].c_str(),
                 fields[3].c_str(), fields[4].c_str());
        displayDebugInfo(debugMsg);

        snprintf(debugMsg, sizeof(debugMsg), "Extracted: PM1.0=%d PM2.5=%d PM10=%d",
                 pm1_0, pm2_5, pm10);
        displayDebugInfo(debugMsg);

        // Add to sums
        pm1_0_sum += pm1_0;
        pm2_5_sum += pm2_5;
        pm10_sum += pm10;
        count++;
    }

    dataFile.close();

    if (count > 0)
    {
        PMReadings avgReading;
        avgReading.pm1_0_atm = pm1_0_sum / count;
        avgReading.pm2_5_atm = pm2_5_sum / count;
        avgReading.pm10_atm = pm10_sum / count;
        avgReading.timestamp = rtc.now().unixtime(); // Use RTC timestamp

        hourlyReadings.push_back(avgReading);

        while (hourlyReadings.size() > 24)
        {
            hourlyReadings.erase(hourlyReadings.begin());
        }

        char debugMsg[100];
        snprintf(debugMsg, sizeof(debugMsg),
                 "Avg calculated: PM1.0=%d PM2.5=%d PM10=%d Count=%d",
                 avgReading.pm1_0_atm, avgReading.pm2_5_atm, avgReading.pm10_atm, count);
        displayDebugInfo(debugMsg);
    }
}

// Function to prepare historical data for BLE transmission
String getHistoricalData()
{
    String data = "";
    uint32_t current_time = rtc.now().unixtime(); // Use RTC timestamp

    for (const auto &reading : hourlyReadings)
    {
        DateTime readingTime(reading.timestamp);
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%02d:%02d,%d,%d,%d\n",
                 readingTime.hour(),
                 readingTime.minute(),
                 reading.pm1_0_atm,
                 reading.pm2_5_atm,
                 reading.pm10_atm);
        data += buffer;
    }

    if (data.length() == 0)
    {
        data = "No historical data available\n";
    }

    char debugMsg[50];
    snprintf(debugMsg, sizeof(debugMsg), "Historical data length: %d", data.length());
    displayDebugInfo(debugMsg);

    return data;
}

void displayTime()
{
    DateTime now = rtc.now();
    char timeStr[20];
    sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());

    tft.setTextColor(TFT_YELLOW);
    tft.setTextSize(1);
    tft.drawString(timeStr, 10, 190); // Display at bottom of screen
}

void setup()
{
    // Initialize serial communication
    Serial.begin(115200);

    // Initialize button and backlight
    pinMode(WIO_5S_PRESS, INPUT_PULLUP);
    pinMode(LCD_BACKLIGHT, OUTPUT);
    digitalWrite(LCD_BACKLIGHT, HIGH); // Turn on backlight initially

    // Initialize display
    tft.begin();
    tft.setRotation(3); // Landscape mode
    tft.fillScreen(TFT_BLACK);
    displayDebugInfo("Display initialized");

    // Initialize SD card
    displayDebugInfo("Initializing SD card...");
    if (!SD.begin(SDCARD_SS_PIN, SDCARD_SPI))
    {
        displayDebugInfo("SD card initialization failed!");
        Serial.println("* Is the SD card inserted?");
        Serial.println("* Is the SD card formatted to FAT32?");
        // Don't halt program if SD fails
        delay(2000);
    }
    else
    {
        displayDebugInfo("SD card initialized.");
        ensureDataDirectory();
    }

    // Initialize RTC
    displayDebugInfo("Initializing RTC...");
    if (!rtc.begin())
    {
        displayDebugInfo("RTC initialization failed!");
        while (1)
            ; // Critical failure - halt program
    }

    // Set RTC time using compile time
    DateTime compile_time(F(__DATE__), F(__TIME__));
    rtc.adjust(compile_time);
    displayDebugInfo("RTC initialized with compile time");

    // Initialize display
    tft.begin();
    tft.setRotation(3); // Landscape mode
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    displayDebugInfo("Display initialized");
    delay(1000); // Give time to see the initialization message

    // Initialize sensor
    if (sensor.init())
    {
        displayDebugInfo("HM330X init failed!");
        while (1)
            ;
    }
    displayDebugInfo("Sensor initialized");

    // Initialize BLE
    BLEDevice::init(DEVICE_NAME);
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    pService = pServer->createService(SERVICE_UUID);

    // Create BLE Characteristics
    pPM1_0Characteristic = pService->createCharacteristic(
        PM1_0_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pPM1_0Characteristic->addDescriptor(new BLE2902());

    pPM2_5Characteristic = pService->createCharacteristic(
        PM2_5_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pPM2_5Characteristic->addDescriptor(new BLE2902());

    pPM10Characteristic = pService->createCharacteristic(
        PM10_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    pPM10Characteristic->addDescriptor(new BLE2902());

    pHistoryCharacteristic = pService->createCharacteristic(
        HISTORY_CHAR_UUID,
        BLECharacteristic::PROPERTY_READ);

    pTimeSyncCharacteristic = pService->createCharacteristic(
        TIME_SYNC_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE);
    pTimeSyncCharacteristic->setCallbacks(new TimeSyncCallback());

    // Start BLE service and advertising
    pService->start();
    pServer->getAdvertising()->start();
    displayDebugInfo("BLE initialized");

    // Show current time on display
    DateTime now = rtc.now();
    char timeStr[32];
    sprintf(timeStr, "Time: %04d-%02d-%02d %02d:%02d:%02d",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());
    displayDebugInfo(timeStr);

    displayDebugInfo("Setup complete!");
}

void loop()
{
    static unsigned long lastUpdate = 0;
    static unsigned long lastLog = 0;
    static unsigned long lastHourlyCalc = 0;
    static unsigned long lastButtonCheck = 0;
    static bool backlightOn = true;

    unsigned long currentMillis = millis();

    // Handle button press for backlight toggle
    if (currentMillis - lastButtonCheck >= 30)
    { // Debounce delay
        if (digitalRead(WIO_5S_PRESS) == LOW)
        {
            backlightOn = !backlightOn;
            digitalWrite(LCD_BACKLIGHT, backlightOn ? HIGH : LOW);
        }
        lastButtonCheck = currentMillis;
    }

    // Regular sensor reading (every 5 seconds)
    if (currentMillis - lastUpdate >= 5000)
    {
        if (!sensor.read_sensor_value(buf, 29))
        {
            updateReadings(buf);
            drawReadings();
            displayTime();
        }
        lastUpdate = currentMillis;
    }

    // Log to SD card (every LOG_INTERVAL)
    if (currentMillis - lastLog >= LOG_INTERVAL)
    {
        logToSD(readings);
        lastLog = currentMillis;
    }

    // Calculate hourly averages (every hour)
    if (currentMillis - lastHourlyCalc >= 3600000)
    {
        calculateHourlyAverage();
        // Update historical data characteristic
        String historicalData = getHistoricalData();
        pHistoryCharacteristic->setValue(historicalData.c_str());
        lastHourlyCalc = currentMillis;
    }
} // End of loop()
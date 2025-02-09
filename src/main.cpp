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
#include <TimeLib.h>
#include <vector>

// BLE Device name
#define DEVICE_NAME "PM2.5 Sensor"

// Custom UUIDs for BLE service and characteristics
#define SERVICE_UUID "91bad492-b950-4226-aa2b-4ede9fa42f59"
#define PM1_0_CHAR_UUID "91bad493-b950-4226-aa2b-4ede9fa42f59"
#define PM2_5_CHAR_UUID "91bad494-b950-4226-aa2b-4ede9fa42f59"
#define PM10_CHAR_UUID "91bad495-b950-4226-aa2b-4ede9fa42f59"
#define HISTORY_CHAR_UUID "91bad496-b950-4226-aa2b-4ede9fa42f59"

// Logging interval (in milliseconds)
#define LOG_INTERVAL 300000 // 5 minutes

// Initialize objects
TFT_eSPI tft;
HM330X sensor;
uint8_t buf[30];

// BLE objects
BLEServer *pServer = nullptr;
BLEService *pService = nullptr;
BLECharacteristic *pPM1_0Characteristic = nullptr;
BLECharacteristic *pPM2_5Characteristic = nullptr;
BLECharacteristic *pPM10Characteristic = nullptr;
BLECharacteristic *pHistoryCharacteristic = nullptr;
bool deviceConnected = false;

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

// Function to update sensor readings
void updateReadings(uint8_t *data)
{
    readings.pm1_0_std = (uint16_t)data[4] << 8 | data[5];
    readings.pm2_5_std = (uint16_t)data[6] << 8 | data[7];
    readings.pm10_std = (uint16_t)data[8] << 8 | data[9];
    readings.pm1_0_atm = (uint16_t)data[10] << 8 | data[11];
    readings.pm2_5_atm = (uint16_t)data[12] << 8 | data[13];
    readings.pm10_atm = (uint16_t)data[14] << 8 | data[15];
    readings.timestamp = now();

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
        // Check for existing files to determine power cycle number
        if (SD.exists("/data/power_cycle.txt"))
        {
            File cycleFile = SD.open("/data/power_cycle.txt", FILE_READ);
            if (cycleFile)
            {
                powerCycle = cycleFile.parseInt();
                cycleFile.close();
            }
        }

        // Increment and save power cycle number
        powerCycle++;
        File cycleFile = SD.open("/data/power_cycle.txt", FILE_WRITE);
        if (cycleFile)
        {
            cycleFile.println(powerCycle);
            cycleFile.close();
        }
        initialized = true;
    }

    char datetime[32];
    unsigned long uptime = millis() / 1000; // Convert to seconds
    sprintf(datetime, "P%03d_%02lu:%02lu:%02lu",
            powerCycle,
            (uptime / 3600) % 24, // Hours
            (uptime / 60) % 60,   // Minutes
            uptime % 60);         // Seconds
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
        if (line.length() == 0) continue;

        // Debug raw line
        snprintf(debugMsg, sizeof(debugMsg), "Raw line: %s", line.c_str());
        displayDebugInfo(debugMsg);

        // Count commas for validation
        int commaCount = 0;
        for (char c : line) {
            if (c == ',') commaCount++;
        }
        
        snprintf(debugMsg, sizeof(debugMsg), "Found %d commas in line", commaCount);
        displayDebugInfo(debugMsg);

        if (commaCount < 4) {
            displayDebugInfo("Not enough commas, skipping line");
            continue;
        }

        // Split the line into fields
        int start = 0;
        int fieldCount = 0;
        String fields[6];  // To store each field
        
        for (int i = 0; i < line.length(); i++) {
            if (line.charAt(i) == ',' || i == line.length() - 1) {
                if (i == line.length() - 1) i++;  // Include last character
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
        avgReading.timestamp = now();

        hourlyReadings.push_back(avgReading);

        while (hourlyReadings.size() > 24)
        {
            hourlyReadings.erase(hourlyReadings.begin());
        }

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
    unsigned long current_time = now();
    
    for (const auto &reading : hourlyReadings)
    {
        // Calculate minutes ago instead of using timestamp
        unsigned long minutes_ago = (current_time - reading.timestamp) / 60;
        
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%lu,%d,%d,%d\n",
                minutes_ago,
                reading.pm1_0_atm,
                reading.pm2_5_atm,
                reading.pm10_atm);
        data += buffer;
    }
    
    if (data.length() == 0) {
        data = "No historical data available\n";
    }
    
    char debugMsg[50];
    snprintf(debugMsg, sizeof(debugMsg), "Historical data length: %d", data.length());
    displayDebugInfo(debugMsg);
    
    return data;
}

void setup()
{

    // Initialize serial communication
    Serial.begin(115200);

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

    // Set initial time (you should implement proper time sync)
    // setTime(0); // Set to epoch start, should be updated with real time

    // Initialize time using compile time
    const char compile_date[] = __DATE__;
    const char compile_time[] = __TIME__;

    // Parse compile time strings
    char month_str[4];
    int day, year, hour, minute, second;
    sscanf(compile_date, "%s %d %d", month_str, &day, &year);
    sscanf(compile_time, "%d:%d:%d", &hour, &minute, &second);

    // Convert month string to number
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int month = 0;
    for (int i = 0; i < 12; i++)
    {
        if (strncmp(month_str, months[i], 3) == 0)
        {
            month = i + 1;
            break;
        }
    }

    // Set time using TimeLib
    setTime(hour, minute, second, day, month, year);

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

    // Start service and advertising
    pService->start();
    pServer->getAdvertising()->start();
}

void loop()
{
    static unsigned long lastUpdate = 0;
    static unsigned long lastLog = 0;
    static unsigned long lastHourlyCalc = 0;

    unsigned long currentMillis = millis();

    // Regular sensor reading (every 5 seconds)
    if (currentMillis - lastUpdate >= 5000)
    {
        if (!sensor.read_sensor_value(buf, 29))
        {
            updateReadings(buf);
            drawReadings();
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
    if (currentMillis - lastHourlyCalc >= 3600000) {
    // if (currentMillis - lastHourlyCalc >= 600000)
    {
        calculateHourlyAverage();
        // Update historical data characteristic
        String historicalData = getHistoricalData();
        pHistoryCharacteristic->setValue(historicalData.c_str());
        lastHourlyCalc = currentMillis;
    }
}
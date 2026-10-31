#if defined(ESP32)
#include "esp32BLEUtilities.h"
#include "DabbleRxRing.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

bool BLE_status = 0;
static std::atomic<bool> isDeviceConnected{false};
bool prevDeviceConnected = false;
uint8_t tx_Value = 0;
static DabbleRxRing rxRing;
static bool rxReset=false;
static portMUX_TYPE rxMux=portMUX_INITIALIZER_UNLOCKED;

BLEServer *bleServer = NULL;
BLECharacteristic *bleTxCharacteristic;

// BLE Server Callbacks
class BleServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* bleServer) override {
        isDeviceConnected = true;
    }

    void onDisconnect(BLEServer* bleServer) override {
        isDeviceConnected = false;
        portENTER_CRITICAL(&rxMux);
        rxRing.clear();
        rxReset=true;
        portEXIT_CRITICAL(&rxMux);
    }
};

// BLE Characteristic Callbacks
class BleCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *bleCharacteristic) override {
        String rx_Value = bleCharacteristic->getValue();  // Use String instead of std::string
        portENTER_CRITICAL(&rxMux);
        // Reject an entire oversized/full packet instead of replacing a buffer
        // concurrently used by loopTask. No heap-owned pointer crosses cores.
        const uint8_t *data=reinterpret_cast<const uint8_t*>(rx_Value.c_str());
        if(!rxRing.append(data,rx_Value.length())){
            rxRing.clear();rxReset=true;
            rxRing.append(data,rx_Value.length());
        }
        portEXIT_CRITICAL(&rxMux);
    }
};

void Esp32ble::begin(String a) {  // Use String as parameter type
    BLEDevice::init(a);

    // Create the BLE Server
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new BleServerCallbacks());

    // Create the BLE Service
    BLEService *bleService = bleServer->createService(UUID_Service);

    // Create a BLE Characteristic
    bleTxCharacteristic = bleService->createCharacteristic(
        UUID_Transmit,
        BLECharacteristic::PROPERTY_NOTIFY
    );

    bleTxCharacteristic->addDescriptor(new BLE2902());

    BLECharacteristic *bleRxCharacteristic = bleService->createCharacteristic(
        UUID_Receive,
        BLECharacteristic::PROPERTY_WRITE
    );

    bleRxCharacteristic->setCallbacks(new BleCallbacks());

    // Start the service
    bleService->start();

    // Start advertising
    bleServer->getAdvertising()->start();

    #ifdef DEBUG
    Serial.println("Waiting for a client connection...");
    #endif
}

void Esp32ble::write(uint8_t a) {
    if (isDeviceConnected) {
        bleTxCharacteristic->setValue(&a, 1);
        bleTxCharacteristic->notify();
        delay(10); // avoid congestion in the BLE stack
    }

    if (!isDeviceConnected && prevDeviceConnected) {
        delay(500);
        bleServer->startAdvertising();
        #ifdef DEBUG
        Serial.println("Start advertising");
        #endif
        prevDeviceConnected = isDeviceConnected.load();
    }
}

void Esp32ble::write(String x) {  // Use String as parameter type
    if (isDeviceConnected) {
        bleTxCharacteristic->setValue(x);  // Use String
        bleTxCharacteristic->notify();
        delay(10);
    }

    if (!isDeviceConnected && prevDeviceConnected) {
        delay(500);
        bleServer->startAdvertising();
        #ifdef DEBUG
        Serial.println("Start advertising");
        #endif
        prevDeviceConnected = isDeviceConnected.load();
    }
}

void Esp32ble::write(int a) {
    if (isDeviceConnected) {
        bleTxCharacteristic->setValue(a);
        bleTxCharacteristic->notify();
        delay(10);
    }

    if (!isDeviceConnected && prevDeviceConnected) {
        delay(500);
        bleServer->startAdvertising();
        #ifdef DEBUG
        Serial.println("Start advertising");
        #endif
        prevDeviceConnected = isDeviceConnected.load();
    }
}

void Esp32ble::write(float a) {
    if (isDeviceConnected) {
        bleTxCharacteristic->setValue(a);
        bleTxCharacteristic->notify();
        delay(10);
    }

    if (!isDeviceConnected && prevDeviceConnected) {
        delay(500);
        bleServer->startAdvertising();
        #ifdef DEBUG
        Serial.println("Start advertising");
        #endif
        prevDeviceConnected = isDeviceConnected.load();
    }
}

uint8_t Esp32ble::available() {
    portENTER_CRITICAL(&rxMux);
    const size_t n=rxRing.size();
    portEXIT_CRITICAL(&rxMux);
    return static_cast<uint8_t>(n>255?255:n);
}

uint8_t Esp32ble::read() {
    portENTER_CRITICAL(&rxMux);
    const uint8_t value=rxRing.pop();
    portEXIT_CRITICAL(&rxMux);
    return value;
}

bool Esp32ble::connected() const { return isDeviceConnected.load(); }
bool Esp32ble::takeRxReset() {
    portENTER_CRITICAL(&rxMux);
    const bool reset=rxReset;rxReset=false;
    portEXIT_CRITICAL(&rxMux);
    return reset;
}

void Esp32ble::stop() {
    btStop();
}

// Define the global instance
Esp32ble esp32ble;

#endif

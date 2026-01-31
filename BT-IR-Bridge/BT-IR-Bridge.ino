#include <NimBLEDevice.h>
#include "mapping.h"

static bool authenticated = false;
static bool doConnect = false;
static NimBLEAdvertisedDevice* advDevice = nullptr;
static NimBLEClient* pClient = nullptr;

void sendNECDirect(uint32_t code);

// --- SYGNAŁ IR ---
void pulse(uint32_t active_us, uint32_t idle_us) {
    digitalWrite(IR_PIN, LOW); 
    delayMicroseconds(active_us);
    digitalWrite(IR_PIN, HIGH);
    if (idle_us > 0) delayMicroseconds(idle_us);
}

void sendNECDirect(uint32_t code) {
    if (code == 0) return;
    Serial.printf(">>> IR: 0x%08X\n", code);
    pulse(9000, 4500);
    for (int i = 0; i < 32; i++) {
        uint32_t bit = (code >> (31 - i)) & 1;
        if (bit) pulse(560, 1690); else pulse(560, 560);
    }
    pulse(560, 0);
}

// --- LOGIKA DOPASOWANIA ---
void findAndSend64(uint8_t* pData) {
    bool isRelease = true;
    for(int i=0; i<8; i++) if(pData[i] != 0) isRelease = false;
    if (isRelease) return;

    for (const auto &item : map64) {
        if (memcmp(pData, item.hexPattern, 8) == 0) {
            Serial.printf("Kliknięto: %s\n", item.name);
            sendNECDirect(item.irNecCode);
            return;
        }
    }
}

void findAndSend71(uint8_t* pData) {
    if (pData[0] == 0 && pData[1] == 0) return;
    for (const auto &item : map71) {
        if (memcmp(pData, item.hexPattern, 2) == 0) {
            Serial.printf("Kliknięto (71): %s\n", item.name);
            
            // Wysyłamy IR jeśli jest zdefiniowane
            if (item.irNecCode != 0) sendNECDirect(item.irNecCode);
            
            // WYWOŁANIE GPIO (np. dla Power)
            if (item.triggerGpio != 0) {
                Serial.printf("Wyzwalanie GPIO %d\n", item.triggerGpio);
                digitalWrite(item.triggerGpio, LOW);
                delay(10);
                digitalWrite(item.triggerGpio, HIGH);
            }
            return;
        }
    }
}

// --- BLE CALLBACKS ---
void notifyCallback(NimBLERemoteCharacteristic* pRemoteChar, uint8_t* pData, size_t len, bool isNotify) {
    uint16_t handle = pRemoteChar->getHandle();
    if (handle == 64 && len >= 8) findAndSend64(pData);
    else if (handle == 71 && len >= 2) findAndSend71(pData);
}

class MyClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) { pClient->secureConnection(); }
    void onDisconnect(NimBLEClient* pClient) { 
        authenticated = false; doConnect = false; 
        NimBLEDevice::getScan()->start(0); 
    }
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) { 
        if (connInfo.isEncrypted()) authenticated = true; 
    }
};

class MyScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) {
        if (advertisedDevice->isAdvertisingService(NimBLEUUID((uint16_t)0x1812))) {
            NimBLEDevice::getScan()->stop();
            advDevice = new NimBLEAdvertisedDevice(*advertisedDevice);
            doConnect = true;
        }
    }
};

void setup() {
    Serial.begin(115200);
    
    pinMode(IR_PIN, OUTPUT);
    digitalWrite(IR_PIN, HIGH);
    
    // Konfiguracja pinu Power jako wyjście (stan wysoki)
    pinMode(PWR_PIN, OUTPUT);
    digitalWrite(PWR_PIN, HIGH); 
    
    NimBLEDevice::init("BT-IR-Bridge");
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(new MyScanCallbacks(), false);
    pScan->start(0);
}

void loop() {
    if (doConnect) {
        pClient = NimBLEDevice::createClient(advDevice->getAddress());
        pClient->setClientCallbacks(new MyClientCallbacks(), false);
        if (pClient->connect()) {
            unsigned long start = millis();
            while (!authenticated && millis() - start < 10000) delay(10);
            if (authenticated) {
                auto services = pClient->getServices(true);
                for (auto& pSvc : services) {
                    if (pSvc->getUUID() == NimBLEUUID((uint16_t)0x1812)) {
                        for (auto& pChr : pSvc->getCharacteristics(true)) {
                            if (pChr->canNotify()) pChr->subscribe(true, notifyCallback);
                        }
                    }
                }
            }
        }
        doConnect = false;
        delete advDevice;
        advDevice = nullptr;
    }
    delay(10);
}

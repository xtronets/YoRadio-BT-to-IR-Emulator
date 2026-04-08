#include <NimBLEDevice.h>
#include "mapping.h"

// --- ZMIENNE GLOBALNE ---
static bool authenticated = false;
static bool doConnect = false;
static NimBLEAdvertisedDevice* advDevice = nullptr;
static NimBLEClient* pClient = nullptr;

// --- FUNKCJA IMPULSU IR (BEZ MODULACJI) ---
void pulse(uint32_t active_us, uint32_t idle_us) {
    digitalWrite(IR_PIN, LOW); // Aktywny (stan niski)
    delayMicroseconds(active_us);
    digitalWrite(IR_PIN, HIGH); // Spoczynek
    if (idle_us > 0) delayMicroseconds(idle_us);
}

void sendNECDirect(uint32_t code) {
    if (code == 0) return;
    Serial.printf(">>> WYSYLAM IR: 0x%08X\n", code);
    
    pulse(9000, 4500); // Start bit
    for (int i = 0; i < 32; i++) {
        uint32_t bit = (code >> (31 - i)) & 1;
        if (bit) pulse(560, 1690); else pulse(560, 560);
    }
    pulse(560, 0); // Stop bit
}

// --- OPTYMALIZACJA BATERII PILOTA ---
void setBatteryFriendlyParams(NimBLEClient* client) {
    // Slave Latency = 20 pozwala pilotowi "spać" przez 20 cykli komunikacji
    // co drastycznie zmniejsza zużycie baterii w pilocie.
    client->updateConnParams(40, 80, 20, 400);
    Serial.println("Parametry baterii ustawione.");
}

// --- LOGIKA PRZYCISKÓW ---
void findAndSend(uint8_t* pData, size_t len, uint16_t handle) {
    bool isRelease = true;
    for(int i=0; i<len; i++) if(pData[i] != 0) isRelease = false;
    if (isRelease) return;

    if (handle == 64 && len >= 8) {
        for (const auto &item : map64) {
            if (memcmp(pData, item.hexPattern, 8) == 0) {
                Serial.printf("Przycisk: %s\n", item.name);
                sendNECDirect(item.irNecCode);
                return;
            }
        }
    } 
    else if (handle == 71 && len >= 2) {
        for (const auto &item : map71) {
            if (memcmp(pData, item.hexPattern, 2) == 0) {
                Serial.printf("Przycisk (71): %s\n", item.name);
                if (item.irNecCode != 0) sendNECDirect(item.irNecCode);
                if (item.triggerGpio != 0) {
                    digitalWrite(item.triggerGpio, LOW);
                    delay(150);
                    digitalWrite(item.triggerGpio, HIGH);
                }
                return;
            }
        }
    }
}

// --- CALLBACKS BLE ---
void notifyCallback(NimBLERemoteCharacteristic* pRemoteChar, uint8_t* pData, size_t len, bool isNotify) {
    findAndSend(pData, len, pRemoteChar->getHandle());
}

class MyClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* client) { 
        Serial.println("Polaczono. Parowanie...");
        client->secureConnection();
    }
    void onDisconnect(NimBLEClient* client) { 
        authenticated = false;
        doConnect = false;
        Serial.println("Pilot rozlaczony. Szukam ponownie...");
        // Kluczowe dla wybudzania po 1 dniu: restart skanowania
        NimBLEDevice::getScan()->start(0, false); 
    }
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) { 
        if (connInfo.isEncrypted()) {
            authenticated = true;
            Serial.println("Autoryzacja OK!");
        }
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
    
    // REDUKCJA CIEPLA: Obnizamy taktowanie do 80MHz
    setCpuFrequencyMhz(80);
    
    pinMode(IR_PIN, OUTPUT);
    digitalWrite(IR_PIN, HIGH);
    pinMode(PWR_PIN, OUTPUT);
    digitalWrite(PWR_PIN, HIGH);
    
    NimBLEDevice::init("BT-IR-Bridge");
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    
    // Mniejsza moc BT = mniej ciepla
    NimBLEDevice::setPower(ESP_PWR_LVL_N0); 

    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setScanCallbacks(new MyScanCallbacks(), false);
    pScan->setInterval(100);
    pScan->setWindow(50);
    pScan->start(0, false);
}

void loop() {
    if (doConnect) {
        doConnect = false;
        pClient = NimBLEDevice::createClient(advDevice->getAddress());
        pClient->setClientCallbacks(new MyClientCallbacks(), false);
        
        if (pClient->connect()) {
            unsigned long start = millis();
            while (!authenticated && millis() - start < 10000) delay(10);
            
            if (authenticated) {
                setBatteryFriendlyParams(pClient);
                
                // POPRAWKA BŁĘDU KOMPILACJI (brak gwiazdek *)
                for (auto pSvc : pClient->getServices(true)) {
                    if (pSvc->getUUID() == NimBLEUUID((uint16_t)0x1812)) {
                        for (auto pChr : pSvc->getCharacteristics(true)) {
                            if (pChr->canNotify()) pChr->subscribe(true, notifyCallback);
                        }
                    }
                }
                Serial.println("Gotowy!");
            } else {
                pClient->disconnect();
            }
        }
        delete advDevice;
        advDevice = nullptr;
    }
    delay(20);
}
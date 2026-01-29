#include <NimBLEDevice.h>

static NimBLEClient* pClient = nullptr;
static bool doConnect = false;
static NimBLEAdvertisedDevice* advDevice;
static bool authenticated = false;

/** Funkcja wywoływana przy odebraniu danych */
void notifyCallback(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    Serial.printf("\n[ODCZYT] Handle: %d | Hex: ", pRemoteCharacteristic->getHandle());
    for (int i = 0; i < length; i++) Serial.printf("%02X ", pData[i]);
    Serial.println();
}

/** Callbacki klienta */
class MyClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        Serial.println(">>> Połączono. Rozpoczynam parowanie...");
        pClient->secureConnection();
    }
    void onDisconnect(NimBLEClient* pClient) {
        Serial.println(">>> Rozłączono. Szukam ponownie...");
        doConnect = false;
        authenticated = false;
        NimBLEDevice::getScan()->start(0);
    }
    void onAuthenticationComplete(NimBLEConnInfo& connInfo) {
        if (connInfo.isEncrypted()) {
            Serial.println(">>> SZYFROWANIE AKTYWNE!");
            authenticated = true;
        } else {
            Serial.println(">>> Błąd parowania.");
        }
    }
};

/** Callbacki skanowania */
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
    Serial.println("=== DIAGNOSTYKA (FINAL FIX) ===");
    NimBLEDevice::init("BT-IR-Bridge");
    
    // Kluczowe dla HID (strzałki/cyfry)
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
            // Czekamy na szyfrowanie max 10 sekund
            while (!authenticated && millis() - start < 10000) { 
                delay(100); 
            }

            if (authenticated) {
                delay(1500); 
                Serial.println("Analizuję kanały...");

                auto services = pClient->getServices(true);
                for (auto& pSvc : services) {
                    auto characteristics = pSvc->getCharacteristics(true);
                    for (auto& pChr : characteristics) {

                        if (pChr->canNotify()) {
                            if (pChr->subscribe(true, notifyCallback)) {
                                Serial.printf("  [+] Nasłuch na Handle: %d OK\n", pChr->getHandle());
                            }
                        }
                    }
                }
                Serial.println(">>> GOTOWE. Testuj przyciski!");
            } else {
                Serial.println(">>> Błąd: Szyfrowanie nie powiodło się.");
            }
        }
        doConnect = false;
        delete advDevice;
        advDevice = nullptr;
    }
    delay(10);
}
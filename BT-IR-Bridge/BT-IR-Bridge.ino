#include <NimBLEDevice.h>
#include "mapping.h"

// ============================================================================
//  BT-IR-Bridge v0.5
//  Wymaga NimBLE-Arduino 2.x
// ============================================================================

#define FW_VERSION "0.5"

// --- USTAWIENIA ---
#define IR_REPEAT_ENABLED   1      // 1 = trzymanie przycisku wysyła ramki NEC "repeat" (np. ciągła zmiana głośności)
#define IR_REPEAT_FIRST_MS  400    // po jakim czasie od wciśnięcia zaczynamy powtarzać
#define IR_REPEAT_EVERY_MS  110    // odstęp między ramkami repeat (NEC: ~108 ms)
#define IR_REPEAT_MAX_MS    10000  // bezpiecznik – gdyby zginęła ramka "puszczenia"
#define GPIO_PULSE_MS       150    // długość impulsu na triggerGpio
#define AUTH_TIMEOUT_MS     10000

// --- ZDARZENIA Z BLE -> loop() ---
// Callback notify działa w tasku hosta NimBLE – nie wolno w nim blokować
// (nadawanie IR trwa ~68 ms, impuls GPIO 150 ms). Dlatego tylko kolejkujemy.
struct HidEvent {
    uint16_t handle;
    uint8_t  len;
    uint8_t  data[8];
};
static QueueHandle_t hidQueue = nullptr;

// --- STAN (współdzielony między taskami -> volatile) ---
static volatile bool doConnect      = false;
static volatile bool restartScan    = false;
static NimBLEAddress targetAddr;
static NimBLEClient* pClient        = nullptr;

// --- STAN TRZYMANEGO PRZYCISKU (tylko loop) ---
static uint32_t heldCode      = 0;
static uint32_t heldSince     = 0;
static uint32_t lastRepeatAt  = 0;

// ============================================================================
//  IR – NEC bez nośnej, aktywny stan niski (wyjście jak z odbiornika TSOP)
// ============================================================================
static inline void pulse(uint32_t active_us, uint32_t idle_us) {
    digitalWrite(IR_PIN, LOW);
    delayMicroseconds(active_us);
    digitalWrite(IR_PIN, HIGH);
    if (idle_us) delayMicroseconds(idle_us);
}

void sendNECDirect(uint32_t code) {
    if (code == 0) return;
    Serial.printf(">>> IR: 0x%08lX\n", (unsigned long)code);
    pulse(9000, 4500);
    for (int i = 0; i < 32; i++) {
        if ((code >> (31 - i)) & 1) pulse(560, 1690);
        else                        pulse(560, 560);
    }
    pulse(560, 0);
}

void sendNECRepeat() {
    pulse(9000, 2250);
    pulse(560, 0);
}

void gpioPulse(int pin) {
    digitalWrite(pin, LOW);
    delay(GPIO_PULSE_MS);
    digitalWrite(pin, HIGH);
}

// ============================================================================
//  MAPOWANIE PRZYCISKÓW (wywoływane z loop)
// ============================================================================
static bool isAllZero(const uint8_t* d, size_t len) {
    for (size_t i = 0; i < len; i++) if (d[i]) return false;
    return true;
}

void handleHidEvent(const HidEvent& ev) {
    if (isAllZero(ev.data, ev.len)) {      // puszczenie przycisku
        heldCode = 0;
        return;
    }

    if (ev.handle == HANDLE_KEYS && ev.len >= 8) {
        for (const auto& item : map64) {
            if (memcmp(ev.data, item.hexPattern, 8) == 0) {
                Serial.printf("Przycisk: %s\n", item.name);
                if (item.irNecCode) {
                    sendNECDirect(item.irNecCode);
                    heldCode = item.irNecCode;
                    heldSince = lastRepeatAt = millis();
                }
                return;
            }
        }
    } else if (ev.handle == HANDLE_MEDIA && ev.len >= 2) {
        for (const auto& item : map71) {
            if (memcmp(ev.data, item.hexPattern, 2) == 0) {
                Serial.printf("Przycisk (media): %s\n", item.name);
                if (item.irNecCode) {
                    sendNECDirect(item.irNecCode);
                    heldCode = item.irNecCode;
                    heldSince = lastRepeatAt = millis();
                }
                if (item.triggerGpio) gpioPulse(item.triggerGpio);
                return;
            }
        }
    }

    // nieznany wzór – przydatne przy dopisywaniu nowych klawiszy
    Serial.printf("Nieznany: handle=%u len=%u :", ev.handle, ev.len);
    for (uint8_t i = 0; i < ev.len; i++) Serial.printf(" %02X", ev.data[i]);
    Serial.println();
}

void handleRepeat() {
#if IR_REPEAT_ENABLED
    if (!heldCode) return;
    uint32_t now = millis();
    if (now - heldSince > IR_REPEAT_MAX_MS) { heldCode = 0; return; }
    if (now - heldSince < IR_REPEAT_FIRST_MS) return;
    if (now - lastRepeatAt >= IR_REPEAT_EVERY_MS) {
        lastRepeatAt = now;
        sendNECRepeat();
    }
#endif
}

// ============================================================================
//  CALLBACKI BLE
// ============================================================================
void notifyCallback(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool isNotify) {
    HidEvent ev{};
    ev.handle = chr->getHandle();
    ev.len    = len > sizeof(ev.data) ? sizeof(ev.data) : len;
    memcpy(ev.data, data, ev.len);
    xQueueSend(hidQueue, &ev, 0);           // nie blokuj hosta BLE
}

class ClientCallbacks : public NimBLEClientCallbacks {
    // UWAGA: w NimBLE 2.x sygnatura ma parametr "reason".
    // Bez niego (jak w starej wersji) metoda NIE nadpisywała bazowej
    // i nigdy nie była wywoływana -> po rozłączeniu skan się nie wznawiał.
    void onDisconnect(NimBLEClient* client, int reason) override {
        Serial.printf("Pilot rozlaczony (reason=%d). Szukam ponownie...\n", reason);
        heldCode = 0;
        restartScan = true;                  // skan uruchomi loop()
    }
    void onConnectFail(NimBLEClient* client, int reason) override {
        Serial.printf("Polaczenie nieudane (reason=%d)\n", reason);
        restartScan = true;
    }
    void onAuthenticationComplete(NimBLEConnInfo& info) override {
        Serial.printf("Autoryzacja: %s, bonded=%d\n",
                      info.isEncrypted() ? "OK" : "BLAD", info.isBonded());
    }
};
static ClientCallbacks clientCallbacks;     // jedna instancja, bez wycieku

class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* dev) override {
        if (doConnect) return;

        const NimBLEAddress& addr = dev->getAddress();
        bool bonded   = NimBLEDevice::isBonded(addr);
        // Sparowane piloty po wybudzeniu często wysyłają "directed advertising"
        // BEZ listy usług – wtedy isAdvertisingService(0x1812) zwraca false.
        bool directed = dev->getAdvType() == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
        bool isHid    = dev->isAdvertisingService(NimBLEUUID((uint16_t)0x1812));

#ifdef REMOTE_MAC
        if (addr != NimBLEAddress(REMOTE_MAC, BLE_ADDR_PUBLIC) &&
            addr != NimBLEAddress(REMOTE_MAC, BLE_ADDR_RANDOM)) return;
#endif
        // Jeśli mamy już sparowanego pilota – łącz tylko z nim.
        // Bez tego bridge podłączy się do dowolnej klawiatury/myszy BLE w zasięgu.
        bool accept = NimBLEDevice::getNumBonds() > 0 ? (bonded || directed)
                                                       : isHid;
        if (!accept) return;

        Serial.printf("Znaleziono: %s (bonded=%d, directed=%d)\n",
                      addr.toString().c_str(), bonded, directed);
        targetAddr = addr;
        doConnect  = true;
        NimBLEDevice::getScan()->stop();
    }
    void onScanEnd(const NimBLEScanResults& results, int reason) override {
        if (!doConnect) restartScan = true;  // skan nie powinien się sam kończyć
    }
};
static ScanCallbacks scanCallbacks;

// ============================================================================
//  POŁĄCZENIE
// ============================================================================
static void startScan() {
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (!scan->isScanning()) scan->start(0, false, true);
}

static bool subscribeHid(NimBLEClient* c) {
    NimBLERemoteService* svc = c->getService(NimBLEUUID((uint16_t)0x1812));
    if (!svc) { Serial.println("Brak uslugi HID"); return false; }

    int n = 0;
    for (auto chr : svc->getCharacteristics(true)) {
        if (chr->canNotify() && chr->subscribe(true, notifyCallback)) {
            Serial.printf("Subskrypcja handle=%u\n", chr->getHandle());
            n++;
        }
    }
    return n > 0;
}

static void connectToRemote() {
    // Używamy JEDNEGO klienta wielokrotnie. Poprzednio każde połączenie robiło
    // createClient() bez deleteClient() – po 3 reconnectach (limit
    // BLE_MAX_CONNECTIONS) createClient() zwracał nullptr i ESP się wysypywał.
    if (!pClient) {
        pClient = NimBLEDevice::createClient();
        pClient->setClientCallbacks(&clientCallbacks, false);
        pClient->setConnectTimeout(5000);
    }

    if (!pClient->connect(targetAddr)) {
        Serial.println("connect() nieudany");
        startScan();                         // poprzednio tu ESP "zawisał" na zawsze
        return;
    }

    // Szyfrowanie wywołujemy tutaj (task loop), a nie w onConnect – blokujące
    // secureConnection() w callbacku hosta BLE może się zakleszczyć.
    // Zastępuje też aktywne czekanie w pętli while.
    if (!pClient->secureConnection()) {
        Serial.println("Szyfrowanie nieudane – rozlaczam");
        pClient->disconnect();
        return;                              // onDisconnect wznowi skan
    }

    // min 50 ms, max 100 ms, latency 20, timeout 6 s.
    // Musi być: timeout > (1 + latency) * interwał_max * 2  =>  >4.2 s.
    // Poprzednie 4 s łamało specyfikację i pilot mógł odrzucić parametry.
    pClient->updateConnParams(40, 80, 20, 600);

    if (!subscribeHid(pClient)) {
        pClient->disconnect();
        return;
    }
    Serial.println("Gotowy!");
}

// ============================================================================
void setup() {
    Serial.begin(115200);
    Serial.println("BT-IR-Bridge v" FW_VERSION);
    setCpuFrequencyMhz(80);                  // minimum dla radia BLE

    pinMode(IR_PIN, OUTPUT);
    digitalWrite(IR_PIN, HIGH);
    pinMode(PWR_PIN, OUTPUT);
    digitalWrite(PWR_PIN, HIGH);

    hidQueue = xQueueCreate(16, sizeof(HidEvent));

    NimBLEDevice::init("BT-IR-Bridge");
    NimBLEDevice::setSecurityAuth(true, true, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    // NimBLE 2.x: setPower() przyjmuje dBm, nie enum!
    // Poprzednie setPower(ESP_PWR_LVL_N0) na C3 dawało ok. +9 dBm (maksimum),
    // czyli odwrotnie niż zamierzano.
    NimBLEDevice::setPower(0);

    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scanCallbacks, false);
    scan->setInterval(100);
    scan->setWindow(50);
    scan->setActiveScan(false);              // pasywny wystarczy, mniej ruchu
    startScan();
}

void loop() {
    HidEvent ev;
    while (xQueueReceive(hidQueue, &ev, 0) == pdTRUE) handleHidEvent(ev);
    handleRepeat();

    if (doConnect) {
        connectToRemote();
        doConnect = false;
    }
    if (restartScan && !doConnect && !(pClient && pClient->isConnected())) {
        restartScan = false;
        startScan();
    }
    delay(5);
}

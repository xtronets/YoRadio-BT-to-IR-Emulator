# YoRadio BT → IR Emulator

Przystawka na ESP32-C3, która pozwala sterować radiem [yoRadio](https://github.com/e2002/yoradio) pilotem **Bluetooth (BLE HID)**.
Bridge paruje się z pilotem, odbiera naciśnięcia przycisków i podaje na wejście IR radia gotowy sygnał **NEC** – radio „myśli”, że dostało komendę z odbiornika podczerwieni.

Dodatkowo jeden przycisk (np. Power) może wysyłać impuls na osobny pin GPIO, którym można usypiać i wybudzać radio z Deep Sleep.

**Aktualna wersja: v0.5** – zobacz [Changelog](#changelog).

---

## Skąd pomysł

Odbiornik IR w moim radiu działał niepewnie, a w domu mam konfigurowalnego pilota [Sofabaton U2](https://www.sofabaton.com/products/u2/) z Bluetooth. Zamiast walczyć z podczerwienią, zrobiłem przystawkę, która paruje się z pilotem BT i udaje odbiornik IR.

## Jak to działa

```
Pilot BLE  ──(Bluetooth HID)──▶  ESP32-C3 (BT-IR-Bridge)  ──IR_PIN (NEC, bez nośnej)──▶  wejście IR yoRadio
                                                          ──PWR_PIN (impuls LOW)──────▶  pin wybudzania yoRadio
```

- Sygnał IR podawany jest **przewodem** prosto na pin odbiornika IR w radiu (bez diody IR i bez nośnej 38 kHz). Stan spoczynkowy wysoki, aktywny niski – tak samo jak na wyjściu odbiornika TSOP.
- Połączenie BLE jest szyfrowane i sparowane (bond), więc po restarcie pilot łączy się ponownie bez parowania.

## Zawartość repozytorium

| Folder / plik | Co to jest |
|---|---|
| [`BT-IR-Bridge/`](BT-IR-Bridge) | **Główny program** na ESP32-C3. Mapowanie przycisków w `mapping.h`. |
| [`Diagnostic-Sniffer/`](Diagnostic-Sniffer) | Narzędzie do odczytu surowych kodów przycisków z Twojego pilota (Serial Monitor). |
| [`yoRadio-plugin/deepsleep.ino`](yoRadio-plugin/deepsleep.ino) | Plugin do **firmware yoRadio** (nie do bridge'a): usypianie/wybudzanie radia przyciskiem Power. |

## Sprzęt

- **Bridge:** ESP32-C3 (testowane na ESP32-C3 Zero / SuperMini)
- **Radio:** yoRadio (testowane z ESP32-S3)
- **Pilot:** dowolny pilot BLE HID; testowane na Sofabaton U2 (profile „Bluetooth keyboard”, „Android box”, „Android TV”)

### Połączenia

| ESP32-C3 (bridge) | yoRadio |
|---|---|
| GPIO 5 (`IR_PIN`) | pin odbiornika IR (zamiast wyjścia TSOP) |
| GPIO 2 (`PWR_PIN`) | pin wybudzania (`WAKEUP_PIN` w `deepsleep.ino`, domyślnie GPIO 2) |
| GND | GND – **wspólna masa jest wymagana** |

Oba układy pracują na logice 3,3 V. Piny zmienisz w `mapping.h`.
Na ESP32-C3 unikaj pinów strappingowych (GPIO 2, 8, 9) do rzeczy, które przy starcie wymuszają stan – GPIO 2 jako wyjście ustawiane w `setup()` działa poprawnie.

## Oprogramowanie

- Arduino IDE z rdzeniem **ESP32 (Espressif) 3.x**
- Biblioteka **[NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) 2.x** (dostępna w Menedżerze bibliotek)
- Radio: yoRadio (testowane na Maestro V7 by Mieczysław Snawacki)

## Instalacja

### 1. Odczyt kodów pilota (opcjonalnie)

Dla popularnych pilotów (Android TV / klawiatura BT) domyślne kody w `mapping.h` zwykle pasują – ten krok możesz pominąć.

1. Wgraj `Diagnostic-Sniffer` na ESP32-C3.
2. Otwórz Serial Monitor (115200).
3. Sparuj pilota i naciskaj przyciski. Zanotuj linie w stylu:
   ```
   [ODCZYT] Handle: 64 | Hex: 00 00 1E 00 00 00 00 00
   ```

Od v0.5 główny program sam wypisuje nieznane przyciski na Serial, więc sniffer przydaje się głównie przy nowym modelu pilota.

### 2. Konfiguracja `mapping.h`

```cpp
const int IR_PIN  = 5;               // wyjście sygnału IR
const int PWR_PIN = 2;               // wyjście impulsu Power

const uint16_t HANDLE_KEYS  = 64;    // raport klawiatury (8 bajtów)
const uint16_t HANDLE_MEDIA = 71;    // raport multimedialny (2 bajty)

// #define REMOTE_MAC "aa:bb:cc:dd:ee:ff"   // opcjonalnie: tylko ten pilot
```

Numery uchwytów (`Handle`) zależą od modelu pilota – wpisz te odczytane snifferem.

Przyciski dodajesz w dwóch tablicach:

```cpp
// map64 – przyciski klawiatury (8 bajtów)
{{0x00,0x00,0x52,0x00,0x00,0x00,0x00,0x00}, 0xFF18E7, "Gora"},

// map71 – przyciski multimedialne: { wzór, kod NEC, nazwa, pin GPIO }
{{0x30, 0x00}, 0x000000, "Power", PWR_PIN},   // tylko impuls na GPIO
{{0x41, 0x00}, 0xFF38C7, "OK",    0},         // tylko kod IR
```

Kod NEC `0x000000` = nie wysyłaj IR. Kody IR muszą odpowiadać tym, których yoRadio oczekuje (ustawienia IR w radiu).

### 3. Wgranie i parowanie

1. Wgraj `BT-IR-Bridge` na ESP32-C3.
2. Włącz parowanie w pilocie – bridge sam znajdzie urządzenie z usługą HID (0x1812), połączy się i sparuje.
3. Na Serial Monitorze pojawi się `Gotowy!`.

> **Pilota trzeba sparować.** Bez parowania działają tylko podstawowe przyciski.
> Po sparowaniu bridge łączy się **tylko z tym pilotem** (nie przejmie klawiatury BT sąsiada). Żeby sparować innego pilota, wyczyść pamięć ESP32 (Erase Flash przy wgrywaniu).

### 4. Plugin Deep Sleep dla yoRadio (opcjonalnie)

`yoRadio-plugin/deepsleep.ino` skopiuj do katalogu firmware yoRadio (obok `yoRadio.ino`) i przekompiluj radio. Daje:

- uśpienie radia impulsem z bridge'a (przycisk Power) – bez czekania na timer,
- wybudzenie tym samym przyciskiem,
- automatyczne uśpienie po `SLEEP_DELAY` sekundach bez odtwarzania (domyślnie 180 s).

## Ustawienia w `BT-IR-Bridge.ino`

| Stała | Domyślnie | Opis |
|---|---|---|
| `IR_REPEAT_ENABLED` | `1` | Przytrzymanie przycisku wysyła ramki NEC „repeat” (np. płynna zmiana głośności) |
| `IR_REPEAT_FIRST_MS` | `400` | Opóźnienie przed pierwszym powtórzeniem |
| `IR_REPEAT_EVERY_MS` | `110` | Odstęp między powtórzeniami |
| `GPIO_PULSE_MS` | `150` | Długość impulsu LOW na pinie GPIO (Power) |

## Rozwiązywanie problemów

- **Pilot nie łączy się ponownie po długiej przerwie** – zaktualizuj do v0.5 (poprawione wznawianie skanu i obsługa directed advertising).
- **Przycisk nic nie robi** – sprawdź Serial: v0.5 wypisuje `Nieznany: handle=… :` z kodem HEX do dopisania w `mapping.h`.
- **Radio nie reaguje na IR** – sprawdź wspólną masę i czy kody NEC zgadzają się z konfiguracją IR w yoRadio.

## Changelog

### v0.5 (31.10.2026)
- Poprawka: `onDisconnect` miał starą sygnaturę (bez `reason`) i w NimBLE 2.x nigdy nie był wywoływany – po rozłączeniu skan nie startował ponownie (bridge i sniffer)
- Poprawka: wyciek klienta BLE (`createClient` przy każdym połączeniu) – crash po kilku reconnectach
- Poprawka: nieudane `connect()` zatrzymywało skanowanie na stałe
- Poprawka: ponowne łączenie sparowanego pilota wysyłającego directed advertising (bez listy usług)
- Po sparowaniu łączenie tylko z pilotem z bondu (opcjonalnie filtr `REMOTE_MAC`)
- Poprawka: `setPower()` przyjmuje dBm – wcześniej ustawiano ok. +9 dBm zamiast 0 dBm
- Poprawka: supervision timeout zgodny ze specyfikacją BLE (6 s)
- IR, impuls GPIO i szyfrowanie przeniesione z callbacków BLE do `loop()` (kolejka FreeRTOS)
- Nowość: ramki NEC repeat przy przytrzymaniu przycisku
- Nowość: logowanie nieznanych przycisków w HEX
- Uchwyty HID jako stałe `HANDLE_KEYS` / `HANDLE_MEDIA`
- `deepsleep.ino` przeniesiony do `yoRadio-plugin/`, uporządkowany README

### 31.01.2026
- Dodany plugin `deepsleep.ino` – wyłączanie radia przyciskiem Power bez czekania na uśpienie

### Pierwsza wersja
- BT-IR-Bridge i Diagnostic-Sniffer

# YoRadio-BT-to-IR-Emulator
Emulator odbiornika IR dla projektu Yoradio


Głównym założeniem projektu było moje niezadowolenie z pracy układu Ir w moim radiu a że mam w domu konfigurowalnego pilota Sofabaton U2 stwierdziłem czemu by tak nie zrobić przystawki do Yoradio która będzie parowała się pilotem BT i udawała odbiornik IR

Pilot https://www.sofabaton.com/products/u2/

Testy były prowadzone na tym pilocie i płytce ESP32-C3 Zero Pro Mini, Oprogramowanie Maestro V7 by Mieczysław Snawacki

Projekt wykorzystuję bibliotekę https://github.com/h2zero/NimBLE-Arduino jest dostępna w menaderze bibliotek w arduino.

Mamy 2 foldery Sniffer oraz główny program 




Projekt pozwala na wykorzystanie taniego pilota Bluetooth (np. od Android TV) jako uniwersalnego kontrolera do urządzeń sterowanych podczerwienią (IR) oraz fizycznego sterowania pinami GPIO (np. do wybudzania radia ze stanu Deep Sleep).

🚀 Możliwości

Sniffing: Wbudowane narzędzie do odczytu surowych ramek HEX bezpośrednio z logów Serial Monitora.


Precyzyjne Mapowanie: Obsługa wielu uchwytów (Handles), co pozwala na rozróżnienie klawiatury numerycznej od przycisków multimedialnych.


IR NEC: Emulacja protokołu NEC dla każdego przycisku.


Hardware Trigger: Możliwość przypisania impulsu GPIO do dowolnego przycisku (np. Power).

🛠 Wymagania Sprzętowe
Mikrokontroler: ESP32-C3 (np. SuperMini).


Nadajnik IR: Emulator IR podłączona do GPIO 5.


Wyjście Sterujące: Pin GPIO 2 (np. do sygnału wybudzania).

📖 Instrukcja Konfiguracji
1. Odczyt kodów (Sniffer)
Zanim skonfigurujesz główne urządzenie, musisz poznać kody swojego pilota:

Wgraj program ze folderu sniffer.

Otwórz Serial Monitor (115200 baud).

Sparuj pilota i naciskaj przyciski.

Zanotuj wartości w formacie: Handle: 64 | Hex: 00 00 1E 00 00 00 00 00 

2. Mapowanie w mapping.h
Otwórz plik mapping.h i uzupełnij tablice zgodnie ze swoimi odczytami:


map64: Dla przycisków standardowych (8 bajtów).


map71: Dla przycisków systemowych/multimedialnych (2 bajty).

Przykład definicji przycisku:

C++
// { {Wzór HEX}, Kod_IR_NEC, "Nazwa", Pin_GPIO }
{{0x30, 0x00}, 0x000000, "Power", PWR_PIN}, // Wyśle impuls na GPIO 2
{{0x41, 0x00}, 0xFF38C7, "OK", 0}            // Wyśle tylko sygnał IR
3. Instalacja
Skonfiguruj piny w mapping.h:

IR_PIN = 5

PWR_PIN = 2

Wgraj główny program na ESP32-C3.

Urządzenie automatycznie wyszuka pilota Bluetooth wspierającego usługę HID (0x1812) i nawiąże bezpieczne, szyfrowane połączenie.

⚠️ Uwagi Techniczne

Bezpieczeństwo: Kod obsługuje parowanie z szyfrowaniem (Secure Connection).


Piny Strappingowe: Unikaj używania GPIO 10 w ESP32-C3, jeśli planujesz wymuszać stan wysoki przy starcie.

Impuls Power: Przycisk przypisany do PWR_PIN generuje 100ms impuls niskiego stanu (LOW), co idealnie nadaje się do wybudzania zewnętrznych układów.

Po wgraniu głównego programu na płytkę piloya nalezy sparować w przeciwnym razie będa działać tylko przyciski podstawowe

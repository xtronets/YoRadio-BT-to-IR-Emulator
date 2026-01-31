/*
 *******************************************************************************************
 * Poprawiona wersja deepsleep.ino dla ESP32-S3 - stabilne uśpienie bez zawieszania
 *******************************************************************************************
 */

#include <Ticker.h>
#include "src/core/options.h"
#include "src/core/display.h"

#define SLEEP_DELAY 180       /* Czas do automatycznego uśpienia (sekundy) */
#define WAKEUP_PIN 2          /* Pin wybudzania i usypiania (GPIO2) */
#define WAKEUP_LEVEL LOW     /* Poziom aktywny (LOW = przycisk do masy) */

#if WAKEUP_PIN != 255
Ticker deepSleepTicker;
Ticker oneShotSleep; // Dodatkowy timer do bezpiecznego wywołania uśpienia

// Funkcja wykonująca faktyczne uśpienie
void executeSleep() {
  // Czyszczenie ekranu i wyłączenie jasności
  dsp.fillRect(0, 0, 256, 64, 0x00);
  dsp.drawLogo(bootLogoTop);
  if (BRIGHTNESS_PIN != 255) analogWrite(BRIGHTNESS_PIN, 0);

  // Sprawdzamy czy uśpienie jest możliwe
  if (display.deepsleep()) {
    // Ponawiamy konfigurację wybudzania tuż przed snem
    esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKEUP_PIN, WAKEUP_LEVEL);
    esp_deep_sleep_start();
  } else {
    deepSleepTicker.detach();
  }
}

// Funkcja wywoływana, gdy radio ma iść spać (bezpieczna dla Ticker i ISR)
void goToSleep() {
  // Zamiast usypiać natychmiast (co zawiesza przerwanie ISR), 
  // planujemy uśpienie za 200ms. To daje czas na zakończenie impulsu 10ms z Bridge.
  oneShotSleep.once_ms(200, executeSleep);
}

void yoradio_on_setup() {
  // Konfiguracja pinu do wybudzania (S3 używa GPIO 2 bez problemu jako RTC IO)
  esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKEUP_PIN, WAKEUP_LEVEL);
  
  pinMode(WAKEUP_PIN, INPUT_PULLUP);
  
  // Przypisanie przerwania - wywołuje goToSleep, które planuje bezpieczne uśpienie
  attachInterrupt(digitalPinToInterrupt(WAKEUP_PIN), goToSleep, FALLING);

  // Uruchomienie licznika automatycznego uśpienia
  deepSleepTicker.attach(SLEEP_DELAY, goToSleep);
}

void player_on_start_play() {
  deepSleepTicker.detach();
}

void player_on_stop_play() {
  deepSleepTicker.attach(SLEEP_DELAY, goToSleep);
}
#endif
/*
 *******************************************************************************************
 * Uwaga! To jest zmodyfikowany plik deepsleep.ino dla YoRadio.
 * Dodano obsługę ręcznego usypiania przyciskiem.
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

// Funkcja wywoływana, gdy radio ma iść spać
void goToSleep() {
  // Czyszczenie ekranu i wyświetlenie logo
  dsp.fillRect(0, 0, 256, 64, 0x00);
  dsp.drawLogo(bootLogoTop);

  // Wyłączenie podświetlenia ekranu
  if (BRIGHTNESS_PIN != 255) analogWrite(BRIGHTNESS_PIN, 0);

  if (display.deepsleep()) {  /* Jeśli sterownik ekranu pozwala na sen */
    esp_deep_sleep_start();   /* Uśpienie procesora ESP32 */
  } else {
    deepSleepTicker.detach();
  }
}

void yoradio_on_setup() {
  // Konfiguracja pinu do wybudzania ze snu
  esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKEUP_PIN, WAKEUP_LEVEL);

  // Konfiguracja pinu do usypiania podczas pracy
  // Ustawiamy rezystor podciągający, aby pin nie "pływał"
  pinMode(WAKEUP_PIN, INPUT_PULLUP);
  
  // Przypisanie przerwania: naciśnięcie wywołuje funkcję goToSleep
  attachInterrupt(digitalPinToInterrupt(WAKEUP_PIN), goToSleep, FALLING);

  // Uruchomienie licznika automatycznego uśpienia
  deepSleepTicker.attach(SLEEP_DELAY, goToSleep);
}

void player_on_start_play() {
  // Wyłączamy timer uśpienia, gdy muzyka gra
  deepSleepTicker.detach();
}

void player_on_stop_play() {
  // Włączamy timer uśpienia ponownie, gdy zatrzymamy muzykę
  deepSleepTicker.attach(SLEEP_DELAY, goToSleep);
}
#endif
#include <Ticker.h>
#include "src/core/options.h"
#include "src/core/display.h"

#define SLEEP_DELAY 180       
#define WAKEUP_PIN 2          
#define WAKEUP_LEVEL LOW     

#if WAKEUP_PIN != 255
Ticker deepSleepTicker;

void goToSleep() {
  if (digitalRead(WAKEUP_PIN) == WAKEUP_LEVEL) {
    while (digitalRead(WAKEUP_PIN) == WAKEUP_LEVEL) {
      delay(10); 
    }
    delay(50); 
  }
  
  dsp.fillRect(0, 0, 256, 64, 0x00);
  dsp.drawLogo(bootLogoTop);


  if (BRIGHTNESS_PIN != 255) analogWrite(BRIGHTNESS_PIN, 0);

 
  if (display.deepsleep()) {
    esp_deep_sleep_start(); 
  } else {
    deepSleepTicker.detach();
  }
}

void yoradio_on_setup() { 
 
  esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKEUP_PIN, WAKEUP_LEVEL);
  
  pinMode(WAKEUP_PIN, INPUT_PULLUP);
  
  attachInterrupt(digitalPinToInterrupt(WAKEUP_PIN), goToSleep, FALLING);

  deepSleepTicker.attach(SLEEP_DELAY, goToSleep);
}

void player_on_start_play() { 
  deepSleepTicker.detach();
}

void player_on_stop_play() { 
  deepSleepTicker.attach(SLEEP_DELAY, goToSleep);
}
#endif

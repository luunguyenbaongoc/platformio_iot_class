#include "led_blinky.h"

void led_blinky(void *pvParameters){
  pinMode(LED_GPIO, OUTPUT);
  
  while(1) {
    if (!isLedBlinkEnabled) {
      digitalWrite(LED_GPIO, LOW);
      xSemaphoreTake(xBinarySemaphoreLedBlink, portMAX_DELAY);
      if (!isLedBlinkEnabled) {
        continue;
      }
    }

    digitalWrite(LED_GPIO, HIGH);  // turn the LED ON
    vTaskDelay(1000);

    if (!isLedBlinkEnabled) {
      digitalWrite(LED_GPIO, LOW);
      continue;
    }

    digitalWrite(LED_GPIO, LOW);  // turn the LED OFF
    vTaskDelay(1000);
    // Serial.println("led_blinky");
  }
}
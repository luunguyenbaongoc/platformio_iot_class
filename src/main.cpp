#include "global.h"

#include "led_blinky.h"

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  // check_info_File(0);

  xTaskCreate(led_blinky, "Task LED Blink", 2048, NULL, 2, NULL);
}

void loop() {
}
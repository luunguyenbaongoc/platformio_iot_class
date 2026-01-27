#include "global.h"

#include "led_blinky.h"
#include "neo_blinky.h"
#include "temp_humi_monitor.h"
#include "mainserver.h"

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  // check_info_File(0);

  xTaskCreate(led_blinky, "Task LED Blink", 2048, NULL, 2, NULL);
  xTaskCreate(neo_blinky, "Task NEO Blink", 2048, NULL, 2, NULL);
  xTaskCreate(temp_humi_monitor, "Task TEMP HUMI Monitor", 2048, NULL, 2, NULL);
  xTaskCreate(main_server_task, "Task Main Server" ,8192  ,NULL  ,2 , NULL);
}

void loop() {
}
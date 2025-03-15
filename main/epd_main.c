/*
 * ESP32S3 with ED047TC1 E-Paper Display Example using epdiy 2.0
 */

 #include <stdio.h>
 #include <string.h>
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "esp_system.h"
 #include "esp_log.h"
 #include "esp_heap_caps.h"
 
 // epdiy library headers for version 2.0
 #include "epdiy.h"
 #include "epd_highlevel.h"
 #include "epd_board_m5papers3.h"
 
 // For debugging
 static const char *TAG = "epd_example";
 
 void app_main(void) {
     ESP_LOGI(TAG, "Starting ED047TC1 E-Paper example with epdiy 2.0");
     
     
 }
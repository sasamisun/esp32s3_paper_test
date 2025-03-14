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
 
 // ED047TC1 display specs (corrected orientation)
 #define DISPLAY_WIDTH 960
 #define DISPLAY_HEIGHT 540
 #define DISPLAY_DEPTH 4 // 16 grayscale levels (4 bits)
 
 // For debugging
 static const char *TAG = "epd_example";
 
 // Global variables
 static uint8_t *fb = NULL;
 static int display_width = 0;
 static int display_height = 0;
 static size_t fb_size = 0;
 
 // Function prototypes
 bool epd_init_display(void);
 void epd_deinit_display(void);
 bool epd_clear_screen(void);
 bool epd_draw_test_pattern(void);
 bool epd_update_screen(enum EpdDrawMode mode);
 
 void app_main(void) {
     ESP_LOGI(TAG, "Starting ED047TC1 E-Paper example with epdiy 2.0");
     
     // Initialize the display
     if (!epd_init_display()) {
         ESP_LOGE(TAG, "Display initialization failed!");
         return;
     }
     
     // Clear the screen to white
     if (!epd_clear_screen()) {
         ESP_LOGE(TAG, "Failed to clear screen!");
         goto cleanup;
     }
     
     vTaskDelay(500 / portTICK_PERIOD_MS);
     
     // Draw test pattern
     if (!epd_draw_test_pattern()) {
         ESP_LOGE(TAG, "Failed to draw test pattern!");
         goto cleanup;
     }

     // Display the content using MODE_GL16 (non-flashing grayscale)
     /*
     if (!epd_update_screen(MODE_GL16)) {
         // If GL16 fails, try GC16
         ESP_LOGW(TAG, "GL16 mode failed, trying GC16...");
         if (!epd_update_screen(MODE_GC16)) {
             ESP_LOGE(TAG, "Failed to update screen!");
         }
     }
    */

     //MODE_DU
     if (!epd_update_screen(MODE_DU)) {
        // If GL16 fails, try GC16
        ESP_LOGW(TAG, "DU mode failed, trying DU...");
        if (!epd_update_screen(MODE_DU)) {
            ESP_LOGE(TAG, "Failed to update screen!");
        }
    }

     vTaskDelay(3000 / portTICK_PERIOD_MS);
     
     ESP_LOGI(TAG, "Demo completed. Display should now show test pattern.");
 
 cleanup:
     // Deinitialize the display and free resources
     epd_deinit_display();
 }
 
 /**
  * Initialize the e-paper display
  * @return true if successful, false otherwise
  */
 bool epd_init_display(void) {
     // Define display
     const EpdDisplay_t display = {
         .width = DISPLAY_WIDTH,
         .height = DISPLAY_HEIGHT,
         .bus_width = 8, // 8-bit parallel bus according to the datasheet
         .bus_speed = 5, // MHz, reduced to avoid cache issues if not set to 64B
     };
     
     // Initialize the display
     epd_init(&epd_board_m5papers3, &display, EPD_OPTIONS_DEFAULT);
     
     // Get actual display dimensions
     display_width = epd_width();
     display_height = epd_height();
     ESP_LOGI(TAG, "Display initialized, resolution: %dx%d", display_width, display_height);
     
     // If display dimensions don't match expected values, warn
     if (display_width != DISPLAY_WIDTH || display_height != DISPLAY_HEIGHT) {
         ESP_LOGW(TAG, "Warning: Display dimensions don't match expected values!");
         ESP_LOGW(TAG, "Expected: %dx%d, Actual: %dx%d", 
                 DISPLAY_WIDTH, DISPLAY_HEIGHT, display_width, display_height);
     }
     
     // Allocate framebuffer memory
     fb_size = display_width * display_height / 2; // 4 bits per pixel
     ESP_LOGI(TAG, "Allocating framebuffer memory of size %d bytes...", fb_size);
     
     fb = heap_caps_malloc(fb_size, MALLOC_CAP_SPIRAM);
     if (fb == NULL) {
         ESP_LOGE(TAG, "Failed to allocate framebuffer in SPIRAM!");
         // Try regular memory
         ESP_LOGI(TAG, "Trying regular memory instead...");
         fb = heap_caps_malloc(fb_size, MALLOC_CAP_8BIT);
         if (fb == NULL) {
             ESP_LOGE(TAG, "Failed to allocate framebuffer!");
             return false;
         }
     }
     
     // Clear the framebuffer (fill with white)
     memset(fb, 0xFF, fb_size);
     ESP_LOGI(TAG, "Framebuffer allocated and cleared");
     
     // Power on the display
     ESP_LOGI(TAG, "Powering on display...");
     epd_poweron();
     vTaskDelay(100 / portTICK_PERIOD_MS);
     
     return true;
 }
 
 /**
  * Deinitialize the display and free resources
  */
 void epd_deinit_display(void) {
     // Power off the display
     ESP_LOGI(TAG, "Powering off display...");
     epd_poweroff();
     
     // Free framebuffer memory
     if (fb != NULL) {
         heap_caps_free(fb);
         fb = NULL;
     }
 }
 
 /**
  * Clear the screen to white
  * @return true if successful, false otherwise
  */
 bool epd_clear_screen(void) {
     ESP_LOGI(TAG, "Clearing display with white...");
     
     EpdRect full_screen = {
         .x = 0,
         .y = 0,
         .width = display_width,
         .height = display_height,
     };
     
     epd_clear_area(full_screen);
     return true;
 }
 
 /**
  * Draw a test pattern on the framebuffer
  * @return true if successful, false otherwise
  */
 bool epd_draw_test_pattern(void) {
     ESP_LOGI(TAG, "Drawing test pattern...");
     
     if (fb == NULL) {
         ESP_LOGE(TAG, "Framebuffer not allocated!");
         return false;
     }
     
     // Fill framebuffer with white
     memset(fb, 0xFF, fb_size);
     
     // Draw a black border (8 pixels wide for visibility)
     for (int i = 0; i < 8; i++) {
         EpdRect border = {
             .x = i,
             .y = i,
             .width = display_width - 2*i,
             .height = display_height - 2*i,
         };
         epd_draw_rect(border, 0, fb);
     }
     
     // Draw some test lines
     epd_draw_line(0, 0, display_width-1, display_height-1, 0, fb);
     epd_draw_line(0, display_height-1, display_width-1, 0, 0, fb);
     
     // Draw a filled circle in the center
     epd_fill_circle(display_width/2, display_height/2, 100, 0, fb);
     
     // Draw some grayscale circles
     for (int i = 0; i < 8; i++) {
         uint8_t gray = i * 16; // 0, 16, 32, ... 112
         epd_draw_circle(display_width/4, display_height/4, 70 - i*8, gray, fb);
     }
     
     // Draw grayscale squares on the right side
     int square_size = 40;
     int start_x = display_width - 200;
     int start_y = (display_height - (16 * square_size)) / 2;
     
     for (int i = 0; i < 16; i++) {
         uint8_t gray = i * 16; // 0, 16, 32, ... 240
         EpdRect square = {
             .x = start_x,
             .y = start_y + i * square_size,
             .width = square_size,
             .height = square_size,
         };
         epd_fill_rect(square, gray, fb);
     }
     
     return true;
 }
 
 /**
  * Update the screen with the current framebuffer content
  * @param mode The drawing mode to use
  * @return true if successful, false otherwise
  */
 bool epd_update_screen(enum EpdDrawMode mode) {
     ESP_LOGI(TAG, "Updating display with mode %d...", mode);
     
     if (fb == NULL) {
         ESP_LOGE(TAG, "Framebuffer not allocated!");
         return false;
     }
     
     EpdRect full_screen = {
         .x = 0,
         .y = 0,
         .width = display_width,
         .height = display_height,
     };
     
     EpdRect no_crop = {
         .x = 0,
         .y = 0,
         .width = 0,
         .height = 0,
     };
     
     // Get current temperature
     float temp = epd_ambient_temperature();
     ESP_LOGI(TAG, "Ambient temperature: %.1f°C", temp);
     int temperature = (int)temp;
     
     // Update display
     enum EpdDrawError err = epd_draw_base(full_screen, fb, no_crop, mode, temperature, NULL, NULL, NULL);
     
     if (err != EPD_DRAW_SUCCESS) {
         ESP_LOGW(TAG, "Display update failed with error: %d", err);
         return false;
     }
     
     ESP_LOGI(TAG, "Display update successful with mode: %d", mode);
     return true;
 }
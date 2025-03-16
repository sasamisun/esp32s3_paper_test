/**
 * @file epd_wrapper.c
 * @brief 電子ペーパーディスプレイ制御のためのラッパーライブラリの実装
 */

 #include <stdio.h>
 #include <string.h>
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "esp_system.h"
 #include "esp_log.h"
 #include "esp_heap_caps.h"
 
 
 #include "epd_board_m5papers3.h"
 #include "epd_wrapper.h"
 
 static const char *TAG = "epd_wrapper";
 
 bool epd_wrapper_init(EPDWrapper *wrapper) {
     if (wrapper == NULL) {
         ESP_LOGE(TAG, "Invalid parameters for initialization");
         return false;
     }
 
     // 構造体を初期化（不定値を避けるため）
     memset(wrapper, 0, sizeof(EPDWrapper));
 
     // EPDIYライブラリの初期化
     ESP_LOGI(TAG, "Initializing display with epdiy library");
     epd_init(&epd_board_m5papers3, &ED047TC1, EPD_LUT_64K);
     
     // 少し待機（初期化が完了するのを待つ）
     vTaskDelay(100 / portTICK_PERIOD_MS);
     
     // ハイレベルAPI初期化
     wrapper->hl_state = epd_hl_init(&epdiy_ED047TC1);
 
     // フレームバッファを取得
     wrapper->framebuffer = epd_hl_get_framebuffer(&wrapper->hl_state);
     if (wrapper->framebuffer == NULL) {
         ESP_LOGE(TAG, "Failed to allocate framebuffer");
         epd_deinit(); // 初期化に失敗したらリソース解放
         return false;
     }
 
     // デフォルト値を設定
     wrapper->is_initialized = true;
     wrapper->is_powered_on = false; // 明示的に電源OFFに設定
     wrapper->rotation = 0;  // デフォルトは0度回転（EPD_ROT_LANDSCAPE）
     
     ESP_LOGI(TAG, "EPD wrapper initialized successfully");
     return true;
 }
 
 void epd_wrapper_deinit(EPDWrapper *wrapper) {
     if (wrapper == NULL || !wrapper->is_initialized) {
         ESP_LOGW(TAG, "EPD wrapper not initialized or already deinitialized");
         return;
     }
 
     // 電源が入っていたら切る
     if (wrapper->is_powered_on) {
         ESP_LOGI(TAG, "Powering off the display before deinit");
         epd_wrapper_power_off(wrapper);
         // 電源OFFが完了するまで少し待機
         vTaskDelay(100 / portTICK_PERIOD_MS);
     }
 
     // EPDIYライブラリの終了処理
     ESP_LOGI(TAG, "Deinitializing epdiy library");
     epd_deinit();
 
     // フレームバッファは epd_hl_init で確保されるので、
     // EPDIYライブラリに任せて解放しない
     wrapper->framebuffer = NULL;
     
     // 構造体の状態をリセット
     wrapper->is_initialized = false;
     wrapper->is_powered_on = false;
     
     ESP_LOGI(TAG, "EPD wrapper deinitialized");
 }
 
 void epd_wrapper_power_on(EPDWrapper *wrapper) {
     if (wrapper == NULL || !wrapper->is_initialized) {
         ESP_LOGE(TAG, "EPD wrapper not initialized");
         return;
     }
 
     if (!wrapper->is_powered_on) {
         // EPDIYライブラリのパネル電源ON
         ESP_LOGI(TAG, "Powering on the display");
         epd_poweron();
         wrapper->is_powered_on = true;
         // 電源が安定するまで少し待機
         vTaskDelay(100 / portTICK_PERIOD_MS);
     } else {
         ESP_LOGW(TAG, "Display is already powered on");
     }
 }
 
 void epd_wrapper_power_off(EPDWrapper *wrapper) {
     if (wrapper == NULL || !wrapper->is_initialized) {
         ESP_LOGE(TAG, "EPD wrapper not initialized");
         return;
     }
 
     if (wrapper->is_powered_on) {
         ESP_LOGI(TAG, "Powering off the display");
         epd_poweroff();
         wrapper->is_powered_on = false;
         // 電源OFFが完了するまで少し待機
         vTaskDelay(100 / portTICK_PERIOD_MS);
     } else {
         ESP_LOGW(TAG, "Display is already powered off");
     }
 }
 
 void epd_wrapper_fill(EPDWrapper *wrapper, uint8_t color) {
     if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL) {
         ESP_LOGE(TAG, "EPD wrapper not properly initialized");
         return;
     }
 
     // 4ビット/ピクセルの場合、2つのピクセルで1バイトを共有
     // すべてのバイトに同じ値を書き込む
     memset(wrapper->framebuffer, color, EPD_DISPLAY_WIDTH * EPD_DISPLAY_HEIGHT / 2);
     ESP_LOGI(TAG, "Framebuffer filled with color 0x%02x", color);
 }
 
 void epd_wrapper_clear_cycles(EPDWrapper *wrapper, int cycles) {
    if (wrapper == NULL || !wrapper->is_initialized) {
        ESP_LOGE(TAG, "EPD wrapper not initialized");
        return;
    }

    if (!wrapper->is_powered_on) {
        ESP_LOGW(TAG, "EPD power is off, turning on for clear cycles");
        epd_wrapper_power_on(wrapper);
    }

    ESP_LOGI(TAG, "Starting %d clear cycles", cycles);

    // 最大サイクル数を制限（安全のため）
    if (cycles > 3) {
        ESP_LOGW(TAG, "Limiting clear cycles to 3 for safety");
        cycles = 3;
    }

    // まず白で塗りつぶす
    ESP_LOGI(TAG, "Initial fill with white");
    epd_wrapper_fill(wrapper, 0xFF);
    epd_wrapper_update_screen(wrapper, MODE_GC16);
    vTaskDelay(300 / portTICK_PERIOD_MS);

    for (int clear_count = 0; clear_count < cycles; clear_count++) {
        ESP_LOGI(TAG, "Clear cycle %d/%d", clear_count + 1, cycles);
        
        // 安全のため、最初のサイクルでのみ黒塗りを行う
        if (clear_count == 0) {
            // 黒で塗りつぶし
            ESP_LOGI(TAG, "Filling with black");
            epd_wrapper_fill(wrapper, 0x00);
            epd_wrapper_update_screen(wrapper, MODE_GC16);
            vTaskDelay(300 / portTICK_PERIOD_MS);
        }
        
        // 白で塗りつぶし
        ESP_LOGI(TAG, "Filling with white");
        epd_wrapper_fill(wrapper, 0xFF);
        epd_wrapper_update_screen(wrapper, MODE_GC16);
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG, "Screen clearing complete");
}
 
 void epd_wrapper_update_screen(EPDWrapper *wrapper, enum EpdDrawMode mode) {
     if (wrapper == NULL || !wrapper->is_initialized) {
         ESP_LOGE(TAG, "EPD wrapper not initialized");
         return;
     }
 
     if (!wrapper->is_powered_on) {
         ESP_LOGW(TAG, "EPD power is off, turning on for update");
         epd_wrapper_power_on(wrapper);
     }
 
     float temperature = epd_ambient_temperature();
     epd_hl_update_screen(&wrapper->hl_state, mode, temperature);
     ESP_LOGI(TAG, "Screen updated with mode %d", mode);
 }
 
 void epd_wrapper_draw_circle(EPDWrapper *wrapper, int x, int y, int radius, uint8_t color) {
     if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL) {
         ESP_LOGE(TAG, "EPD wrapper not properly initialized");
         return;
     }
 
     epd_draw_circle(x, y, radius, color, wrapper->framebuffer);
 }
 
 void epd_wrapper_fill_circle(EPDWrapper *wrapper, int x, int y, int radius, uint8_t color) {
     if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL) {
         ESP_LOGE(TAG, "EPD wrapper not properly initialized");
         return;
     }
 
     epd_fill_circle(x, y, radius, color, wrapper->framebuffer);
 }
 
 void epd_wrapper_draw_line(EPDWrapper *wrapper, int x0, int y0, int x1, int y1, uint8_t color) {
     if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL) {
         ESP_LOGE(TAG, "EPD wrapper not properly initialized");
         return;
     }
 
     epd_draw_line(x0, y0, x1, y1, color, wrapper->framebuffer);
 }
 
 void epd_wrapper_draw_rect(EPDWrapper *wrapper, int x, int y, int width, int height, uint8_t color) {
     if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL) {
         ESP_LOGE(TAG, "EPD wrapper not properly initialized");
         return;
     }
 
     EpdRect rect = {
         .x = x,
         .y = y,
         .width = width,
         .height = height
     };
     
     epd_draw_rect(rect, color, wrapper->framebuffer);
 }
 
 void epd_wrapper_fill_rect(EPDWrapper *wrapper, int x, int y, int width, int height, uint8_t color) {
     if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL) {
         ESP_LOGE(TAG, "EPD wrapper not properly initialized");
         return;
     }
 
     EpdRect rect = {
         .x = x,
         .y = y,
         .width = width,
         .height = height
     };
     
     epd_fill_rect(rect, color, wrapper->framebuffer);
 }
 
 void epd_wrapper_draw_image(EPDWrapper *wrapper, int x, int y, int width, int height, const uint8_t *image_data) {
     if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL || image_data == NULL) {
         ESP_LOGE(TAG, "EPD wrapper not properly initialized or invalid image data");
         return;
     }
 
     EpdRect image_area = {
         .x = x,
         .y = y,
         .width = width,
         .height = height
     };
     
     epd_copy_to_framebuffer(image_area, image_data, wrapper->framebuffer);
 }
 
 void epd_wrapper_draw_grayscale_test(EPDWrapper *wrapper, int x, int y, int width, int height) {
     if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL) {
         ESP_LOGE(TAG, "EPD wrapper not properly initialized");
         return;
     }
 
     // グレースケールパターンを描画（16段階）
     for (int i = 0; i < 16; i++) {
         int pattern_x = x + i * (width / 16);
         int pattern_width = width / 16;
         
         for (int dy = 0; dy < height; dy++) {
             for (int dx = 0; dx < pattern_width; dx++) {
                 int pos = (y + dy) * EPD_DISPLAY_WIDTH + (pattern_x + dx);
                 int byte_pos = pos / 2;
                 
                 if (pos % 2 == 0) {
                     // 偶数ピクセルは下位4ビット
                     wrapper->framebuffer[byte_pos] = (wrapper->framebuffer[byte_pos] & 0xF0) | i;
                 } else {
                     // 奇数ピクセルは上位4ビット
                     wrapper->framebuffer[byte_pos] = (wrapper->framebuffer[byte_pos] & 0x0F) | (i << 4);
                 }
             }
         }
     }
 }
 
 uint8_t* epd_wrapper_get_framebuffer(EPDWrapper *wrapper) {
     if (wrapper == NULL || !wrapper->is_initialized) {
         ESP_LOGE(TAG, "EPD wrapper not initialized");
         return NULL;
     }
     
     return wrapper->framebuffer;
 }
 
 bool epd_wrapper_set_rotation(EPDWrapper *wrapper, int rotation) {
     if (wrapper == NULL || !wrapper->is_initialized) {
         ESP_LOGE(TAG, "EPD wrapper not initialized");
         return false;
     }
     
     // 0〜3の値であることを確認
     if (rotation < 0 || rotation > 3) {
         ESP_LOGE(TAG, "Invalid rotation value: %d (must be 0-3)", rotation);
         return false;
     }
     
     // 回転設定を保存
     wrapper->rotation = rotation;
     
     // epdiyライブラリの回転モードに変換
     enum EpdRotation epd_rotation;
     switch (rotation) {
         case 0:
             epd_rotation = EPD_ROT_LANDSCAPE;        // 0度回転
             break;
         case 1:
             epd_rotation = EPD_ROT_PORTRAIT;         // 90度回転
             break;
         case 2:
             epd_rotation = EPD_ROT_INVERTED_LANDSCAPE; // 180度回転
             break;
         case 3:
             epd_rotation = EPD_ROT_INVERTED_PORTRAIT;  // 270度回転
             break;
         default:
             epd_rotation = EPD_ROT_PORTRAIT;        // デフォルト
             break;
     }
     
     // epdiyライブラリに回転を設定
     epd_set_rotation(epd_rotation);
     
     ESP_LOGI(TAG, "Display rotation set to %d (%d degrees)", rotation, rotation * 90);
     return true;
 }
 
 int epd_wrapper_get_rotation(EPDWrapper *wrapper) {
     if (wrapper == NULL || !wrapper->is_initialized) {
         ESP_LOGE(TAG, "EPD wrapper not initialized");
         return -1;
     }
     
     return wrapper->rotation;
 }
 
 int epd_wrapper_get_width(EPDWrapper *wrapper) {
     if (wrapper == NULL || !wrapper->is_initialized) {
         ESP_LOGE(TAG, "EPD wrapper not initialized");
         return 0;
     }
     
     // 90度または270度回転の場合は幅と高さが入れ替わる
     if (wrapper->rotation == 1 || wrapper->rotation == 3) {
         return EPD_DISPLAY_HEIGHT;
     } else {
         return EPD_DISPLAY_WIDTH;
     }
 }
 
 int epd_wrapper_get_height(EPDWrapper *wrapper) {
     if (wrapper == NULL || !wrapper->is_initialized) {
         ESP_LOGE(TAG, "EPD wrapper not initialized");
         return 0;
     }
     
     // 90度または270度回転の場合は幅と高さが入れ替わる
     if (wrapper->rotation == 1 || wrapper->rotation == 3) {
         return EPD_DISPLAY_WIDTH;
     } else {
         return EPD_DISPLAY_HEIGHT;
     }
 }
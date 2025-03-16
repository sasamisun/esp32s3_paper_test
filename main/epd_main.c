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

#include "ayamelogo4bit.h"

// For debugging
static const char *TAG = "epd_example";

void app_main(void)
{
    // 表示状態を保持する変数
    EpdiyHighlevelState hl;

    ESP_LOGI(TAG, "Starting ED047TC1 E-Paper example with epdiy 2.0");

    // ディスプレイの初期化
    ESP_LOGI(TAG, "Initializing ED047TC1 display");
    epd_init(&epd_board_m5papers3, &ED047TC1, EPD_LUT_64K);
    hl = epd_hl_init(&epdiy_ED047TC1);

    // フレームバッファのメモリ確保
    uint8_t *fb = epd_hl_get_framebuffer(&hl);

    // 電源ON
    epd_poweron();

    // 画面を完全に消去するための複数回のリフレッシュ
    for (int clear_count = 0; clear_count < 3; clear_count++) {
        ESP_LOGI(TAG, "Clear cycle %d/3", clear_count + 1);
        
        // フレームバッファを白で塗りつぶし (0xFF)
        memset(fb, 0xFF, DISPLAY_WIDTH * DISPLAY_HEIGHT / 2);
        
        // 全面アップデートモードで更新
        ESP_LOGI(TAG, "Updating display with white");
        epd_hl_update_screen(&hl, MODE_GC16, epd_board_m5papers3.get_temperature());
        
        // 少し待機
        vTaskDelay(500 / portTICK_PERIOD_MS);
        
        // 異なるパターンでもう一度更新（消去の効果を高めるため）
        if (clear_count == 0) {
            // 一回目は黒で塗りつぶし
            memset(fb, 0x00, DISPLAY_WIDTH * DISPLAY_HEIGHT / 2);
            ESP_LOGI(TAG, "Updating display with black");
            epd_hl_update_screen(&hl, MODE_GC16, epd_board_m5papers3.get_temperature());
            
            vTaskDelay(500 / portTICK_PERIOD_MS);
        }
    }

    // 最終的に白で完全にクリア
    memset(fb, 0xFF, DISPLAY_WIDTH * DISPLAY_HEIGHT / 2);
    ESP_LOGI(TAG, "Final clear to white");
    epd_hl_update_screen(&hl, MODE_GC16, epd_board_m5papers3.get_temperature());

    ESP_LOGI(TAG, "Screen clearing complete");

    
    // 基本図形の描画
    ESP_LOGI(TAG, "Drawing test patterns");

    // 中央に大きな円を描画
    int center_x = DISPLAY_WIDTH / 2;
    int center_y = DISPLAY_HEIGHT / 2;
    int radius = 100;
    epd_draw_circle(center_x, center_y, radius, 0, fb);

    // 対角線を描画
    epd_draw_line(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, fb);
    epd_draw_line(0, DISPLAY_HEIGHT, DISPLAY_WIDTH, 0, 0, fb);
        // 中央付近に小さな円を何個か描画
        epd_fill_circle(center_x - 50, center_y - 50, 20, 0, fb);
        epd_fill_circle(center_x + 50, center_y - 50, 20, 0, fb);
        epd_fill_circle(center_x, center_y + 50, 30, 0, fb);
    // 枠線を描画
    EpdRect border = {
        .x = 10,
        .y = 10,
        .width = DISPLAY_WIDTH - 20,
        .height = DISPLAY_HEIGHT - 20};
    epd_draw_rect(border, 0, fb);

    // ロゴを描画
    ESP_LOGI(TAG, "Drawing Ayame logo");

    // 右上にロゴを配置
    int logo_x = DISPLAY_WIDTH - LOGO_WIDTH - 20;
    int logo_y = 20;

    // ロゴ領域の定義
    EpdRect logo_area = {
        .x = logo_x,
        .y = logo_y,
        .width = LOGO_WIDTH,
        .height = LOGO_HEIGHT};

    // フレームバッファにロゴをコピー
    epd_copy_to_framebuffer(logo_area, logo_data, fb);

    ESP_LOGI(TAG, "Logo drawn at position %d,%d", logo_x, logo_y);

    // グレースケールのテストパターンを直接書き込む
    for (int i = 0; i < 16; i++)
    {
        int x = 100 + i * 30;
        int y = 100;
        int width = 25;
        int height = 150;

        for (int dy = 0; dy < height; dy++)
        {
            for (int dx = 0; dx < width; dx++)
            {
                int pos = (y + dy) * DISPLAY_WIDTH + (x + dx);
                int byte_pos = pos / 2;
                if (pos % 2 == 0)
                {
                    // 偶数ピクセルは下位4ビット
                    fb[byte_pos] = (fb[byte_pos] & 0xF0) | i;
                }
                else
                {
                    // 奇数ピクセルは上位4ビット
                    fb[byte_pos] = (fb[byte_pos] & 0x0F) | (i << 4);
                }
            }
        }
    }

    // フレームバッファの内容を表示
    ESP_LOGI(TAG, "Updating display");
    epd_hl_update_screen(&hl, MODE_GC16, epd_board_m5papers3.get_temperature());


    // 処理完了
    ESP_LOGI(TAG, "Display update complete");

    // 電源OFF
    epd_poweroff();

    // メモリ解放
    free(fb);
}
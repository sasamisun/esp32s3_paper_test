/*
 * ESP32S3 with ED047TC1 E-Paper Display Example using epdiy 2.0
 * with the EPD Wrapper Library
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

// Custom wrapper library
#include "epd_wrapper.h"

// サンプル画像
#include "ayamelogo4bit.h"

// For debugging
static const char *TAG = "epd_example";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ED047TC1 E-Paper example with epdiy 2.0 and EPD Wrapper");

    // EPD Wrapperの状態変数
    EPDWrapper epd;
    memset(&epd, 0, sizeof(EPDWrapper)); // 初期化前に構造体をクリア

    // EPD Wrapperの初期化
    ESP_LOGI(TAG, "Initializing EPD Wrapper");
    if (!epd_wrapper_init(&epd))
    {
        ESP_LOGE(TAG, "Failed to initialize EPD Wrapper");
        return;
    }

    // 初期化後、少し待機
    vTaskDelay(200 / portTICK_PERIOD_MS);

    // 電源ON
    ESP_LOGI(TAG, "Powering on the display");
    epd_wrapper_power_on(&epd);

    // 電源ON後、少し待機
    vTaskDelay(200 / portTICK_PERIOD_MS);

    // ディスプレイのクリア（2サイクル - 少し減らして安全に）
    ESP_LOGI(TAG, "Clearing the display");
    epd_wrapper_clear_cycles(&epd, 2);

    // 回転設定（0:0度, 1:90度, 2:180度, 3:270度）
    // 回転を変更すると、描画する座標系が変わります
    int rotation = 0; // デフォルトの回転（横向き）

    ESP_LOGI(TAG, "Setting display rotation to %d (%d degrees)", rotation, rotation * 90);
    epd_wrapper_set_rotation(&epd, rotation);

    // 回転を考慮したディスプレイサイズを取得
    int display_width = epd_wrapper_get_width(&epd);
    int display_height = epd_wrapper_get_height(&epd);

    ESP_LOGI(TAG, "Display dimensions after rotation: %d x %d", display_width, display_height);

    // 基本図形の描画
    ESP_LOGI(TAG, "Drawing test patterns");

    // 中央に大きな円を描画
    int center_x = display_width / 2;
    int center_y = display_height / 2;
    int radius = 100;
    epd_wrapper_draw_circle(&epd, center_x, center_y, radius, 0);

    // 位置チェック矩形
    epd_wrapper_fill_rect(&epd, 0, 0, 20, 20, 0);
    epd_wrapper_fill_rect(&epd, display_width - 20, display_height - 20, 20, 20, 0);

    // 対角線を描画
    epd_wrapper_draw_line(&epd, 0, 0, display_width, display_height, 0);
    epd_wrapper_draw_line(&epd, 0, display_height, display_width, 0, 0);

    // 中央付近に小さな円をいくつか描画
    epd_wrapper_fill_circle(&epd, center_x - 50, center_y - 50, 20, 0);
    epd_wrapper_fill_circle(&epd, center_x + 50, center_y - 50, 20, 0);
    epd_wrapper_fill_circle(&epd, center_x, center_y + 50, 30, 0);

    // 枠線を描画
    epd_wrapper_draw_rect(&epd, 10, 10, display_width - 20, display_height - 20, 0);

    // ロゴを描画
    ESP_LOGI(TAG, "Drawing Ayame logo");

    // 右上にロゴを配置
    int logo_x = 0;
    int logo_y = 0;

    // フレームバッファにロゴをコピー
    //     epd_wrapper_draw_image(&epd, logo_x, logo_y, LOGO_WIDTH, LOGO_HEIGHT, logo_data);
    //epd_wrapper_draw_rotated_image(&epd, logo_x, logo_y, LOGO_WIDTH, LOGO_HEIGHT, logo_data, true);
    uint8_t transparent_color = 0x0F;
    epd_wrapper_draw_rotated_image_with_transparency(&epd, logo_x, logo_y, LOGO_WIDTH, LOGO_HEIGHT, logo_data, true, true, transparent_color);

    ESP_LOGI(TAG, "Logo drawn at position %d,%d", logo_x, logo_y);

    // グレースケールのテストパターンを描画
    epd_wrapper_draw_grayscale_test(&epd, 100, 100, display_width / 2, 150);

    // フレームバッファの内容を表示
    ESP_LOGI(TAG, "Updating display");
    epd_wrapper_update_screen(&epd, MODE_GC16);

    // ----- 回転テストはいったん除外 ----- //

    // 5秒待機
    vTaskDelay(5000 / portTICK_PERIOD_MS);

    // 90度回転させて再描画
    rotation = 3; // 90度回転
    ESP_LOGI(TAG, "Changing rotation to %d (%d degrees)", rotation, rotation * 90);

    // ディスプレイをクリア
    epd_wrapper_fill(&epd, 0xFF);

    // 回転を設定
    epd_wrapper_set_rotation(&epd, rotation);

    // 回転を考慮したディスプレイサイズを再取得
    display_width = epd_wrapper_get_width(&epd);
    display_height = epd_wrapper_get_height(&epd);

    ESP_LOGI(TAG, "Display dimensions after rotation: %d x %d", display_width, display_height);

    // 再度描画テスト
    center_x = display_width / 2;
    center_y = display_height / 2;

    // 中央に大きな円と枠線を描画
    epd_wrapper_draw_circle(&epd, center_x, center_y, radius, 0);
    epd_wrapper_draw_rect(&epd, 10, 10, display_width - 20, display_height - 20, 0);

    // 位置チェック矩形
    epd_wrapper_fill_rect(&epd, 0, 0, 20, 20, 0);
    epd_wrapper_fill_rect(&epd, display_width - 20, display_height - 20, 20, 20, 0);

    // ロゴを左上に描画
    // epd_wrapper_draw_image(&epd, 20, 20, LOGO_WIDTH, LOGO_HEIGHT, logo_data);
    logo_x = center_x - (LOGO_WIDTH / 2);
    logo_y = center_y - (LOGO_HEIGHT / 2);
    epd_wrapper_draw_rotated_image_with_transparency(&epd, logo_x, logo_y, LOGO_WIDTH, LOGO_HEIGHT, logo_data, true, true, transparent_color);

    //epd_wrapper_draw_rotated_image(&epd, 20, 20, LOGO_WIDTH, LOGO_HEIGHT, logo_data, true);

    // 更新
    epd_wrapper_update_screen(&epd, MODE_GC16);

    // 処理完了
    ESP_LOGI(TAG, "Display update complete");

    // 更新後しばらく待機
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // 電源OFF
    ESP_LOGI(TAG, "Powering off the display");
    epd_wrapper_power_off(&epd);

    // 電源OFFが完了するまで待機
    vTaskDelay(200 / portTICK_PERIOD_MS);

    // EPD Wrapperの終了処理
    ESP_LOGI(TAG, "Deinitializing EPD Wrapper");
    epd_wrapper_deinit(&epd);

    ESP_LOGI(TAG, "Example completed successfully");
}
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
#include "epd_transition.h"

// サンプル画像
#include "ayamelogo4bit.h"
// #include "bg1.h"
#include "bg2.h"
// #include "ichi.h"

// epd_main.c のインクルード部分に追加
#include "epd_text.h"
#include "Mplus2-Light_16.h"

// For debugging
static const char *TAG = "epd_example";

void draw_sprash(EPDWrapper *wrapper);
void transition(EPDWrapper *epd, const uint8_t *newimage, TransitionType type);
void test_text_display(EPDWrapper *wrapper);

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
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // 電源ON
    ESP_LOGI(TAG, "Powering on the display");
    epd_wrapper_power_on(&epd);

    // 電源ON後、少し待機
    vTaskDelay(100 / portTICK_PERIOD_MS);

    // 描画
    draw_sprash(&epd);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    // epd_wrapper_draw_rotated_image(&epd, 0, 0, BG1_WIDTH, BG1_HEIGHT, bg2_data, true);
    // epd_wrapper_update_screen(&epd, MODE_GC16);

    // vTaskDelay(500 / portTICK_PERIOD_MS);
    // transition(&epd, ich_data, TRANSITION_WIPE);
    // vTaskDelay(500 / portTICK_PERIOD_MS);
    // transition(&epd, bg1_data, TRANSITION_SLIDE_LEFT);
    // vTaskDelay(500 / portTICK_PERIOD_MS);
    //transition(&epd, bg2_data, TRANSITION_SLIDE_UP);

    // テキスト表示テスト
    test_text_display(&epd);
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

void draw_sprash(EPDWrapper *epd)
{
    // ディスプレイのクリア（2サイクル - 少し減らして安全に）
    ESP_LOGI(TAG, "Clearing the display");
    epd_wrapper_clear_cycles(epd, 3);

    // 90度回転させて再描画
    uint8_t rotation = 3; // 90度回転
    ESP_LOGI(TAG, "Changing rotation to %d (%d degrees)", rotation, rotation * 90);

    // 回転を設定
    epd_wrapper_set_rotation(epd, rotation);

    // 回転を考慮したディスプレイサイズを再取得
    int display_width = epd_wrapper_get_width(epd);
    int display_height = epd_wrapper_get_height(epd);

    ESP_LOGI(TAG, "Display dimensions after rotation: %d x %d", display_width, display_height);

    // 中央の座標
    int center_x = display_width / 2;
    int center_y = display_height / 2;

    // 中央に大きな円と枠線を描画
    epd_wrapper_draw_circle(epd, center_x, center_y, 100, 0);
    epd_wrapper_draw_rect(epd, 10, 10, display_width - 20, display_height - 20, 0);

    // 位置チェック矩形
    epd_wrapper_fill_rect(epd, 0, 0, 20, 20, 0);
    epd_wrapper_fill_rect(epd, display_width - 20, display_height - 20, 20, 20, 0);

    // ロゴを左上に描画
    // epd_wrapper_draw_image(&epd, 20, 20, LOGO_WIDTH, LOGO_HEIGHT, logo_data);
    int logo_x = center_x - (LOGO_WIDTH / 2);
    int logo_y = center_y - (LOGO_HEIGHT / 2);
    uint8_t transparent_color = 0x0F;
    epd_wrapper_draw_rotated_image_with_transparency(epd, logo_x, logo_y, LOGO_WIDTH, LOGO_HEIGHT, logo_data, true, true, transparent_color);

    // epd_wrapper_draw_rotated_image(&epd, 20, 20, LOGO_WIDTH, LOGO_HEIGHT, logo_data, true);

    // 更新
    epd_wrapper_update_screen(epd, MODE_GC16);

    // 処理完了
    ESP_LOGI(TAG, "Display update complete");
}

void transition(EPDWrapper *epd, const uint8_t *image_data, TransitionType type)
{
    ESP_LOGI(TAG, "Starting transition with type %d", type);

    // 1. トランジション機能の初期化（8ステップを使用）
    EPDTransition transition;
    if (!epd_transition_init(epd, &transition, 8))
    {
        ESP_LOGE(TAG, "Failed to initialize transition");
        return;
    }

    // 2. 次の画面のフレームバッファを取得
    uint8_t *next_fb = epd_transition_get_next_framebuffer(&transition);
    if (next_fb == NULL)
    {
        ESP_LOGE(TAG, "Failed to get next framebuffer");
        epd_transition_deinit(epd, &transition);
        return;
    }

    // 3. 次の画面をフレームバッファBに描画
    // 現在の画面のサイズを取得
    int display_width = epd_wrapper_get_width(epd);
    int display_height = epd_wrapper_get_height(epd);

    // 現在の回転角度を取得
    int rotation = epd_wrapper_get_rotation(epd);
    ESP_LOGI(TAG, "Current display rotation: %d", rotation);

    // 画像データ直接指定の場合
    if (image_data != NULL)
    {
        // まず次のフレームバッファを白でクリア
        memset(next_fb, 0xFF, display_width * display_height / 2);

        // 回転に応じて処理を分岐
        if (rotation != 0)
        {
            // 回転が必要な場合は一時バッファに回転済みデータを作成
            size_t image_bytes = display_width * display_height / 2;
            uint8_t *rotated_data = heap_caps_malloc(image_bytes, MALLOC_CAP_8BIT);

            if (rotated_data != NULL)
            {
                // 一時バッファをクリア（回転処理で使用するビット操作のため重要）
                memset(rotated_data, 0, image_bytes);

                // 回転元のサイズと回転後のサイズが異なる可能性に注意
                int src_width, src_height;

                // 回転に応じて元の幅と高さを設定
                if (rotation == 1 || rotation == 3)
                {
                    // 90度/270度回転の場合は幅と高さが入れ替わる
                    src_width = EPD_DISPLAY_HEIGHT;
                    src_height = EPD_DISPLAY_WIDTH;
                }
                else
                {
                    src_width = EPD_DISPLAY_WIDTH;
                    src_height = EPD_DISPLAY_HEIGHT;
                }

                ESP_LOGI(TAG, "Rotating image data: src=%dx%d, rotation=%d",
                         src_width, src_height, rotation);

                // 画像データを回転
                if (rotate_image_data(image_data, src_width, src_height, rotation, rotated_data) != 0)
                {
                    ESP_LOGE(TAG, "Failed to rotate image data");
                    heap_caps_free(rotated_data);
                    epd_transition_deinit(epd, &transition);
                    return;
                }

                // 回転済みデータをフレームバッファBにコピー
                memcpy(next_fb, rotated_data, image_bytes);

                // 一時バッファを解放
                heap_caps_free(rotated_data);
                ESP_LOGI(TAG, "Image data rotated and copied to next framebuffer");
            }
            else
            {
                ESP_LOGE(TAG, "Failed to allocate memory for rotated image data");
                epd_transition_deinit(epd, &transition);
                return;
            }
        }
        else
        {
            // 回転なしの場合は直接コピー
            // 全画面用の矩形を定義
            EpdRect image_area = {
                .x = 0,
                .y = 0,
                .width = display_width,
                .height = display_height};

            // 画像データをnext_fbにコピー
            epd_copy_to_framebuffer(image_area, image_data, next_fb);
            ESP_LOGI(TAG, "Copied image data to next_fb without rotation");
        }
    }
    else
    {
        // 画像データが指定されていない場合は白で塗りつぶし
        memset(next_fb, 0xFF, display_width * display_height / 2);
        ESP_LOGW(TAG, "No image data provided, using blank white screen");
    }

    // 4. トランジションを準備（高品質モードを使用）
    if (!epd_transition_prepare(epd, &transition, type, MODE_GC16))
    {
        ESP_LOGE(TAG, "Failed to prepare transition");
        epd_transition_deinit(epd, &transition);
        return;
    }

    // 5. トランジションを段階的に実行
    ESP_LOGI(TAG, "Executing transition steps");
    while (transition.is_active)
    {
        if (!epd_transition_step(epd, &transition))
        {
            ESP_LOGE(TAG, "Transition step failed");
            break;
        }

        // 各ステップ間で少し待機（トランジションの速度調整）
        vTaskDelay(150 / portTICK_PERIOD_MS);
    }

    // 6. トランジションリソースの解放
    ESP_LOGI(TAG, "Transition completed, releasing resources");
    epd_transition_deinit(epd, &transition);
}

// 既存の app_main 関数に追加するテキスト表示のテスト関数
void test_text_display(EPDWrapper *epd)
{
    ESP_LOGI(TAG, "Starting text display test");
    // テキスト設定の初期化
    ESP_LOGI(TAG, "Initializing text configuration");
    EPDTextConfig text_config;
    epd_text_config_init(&text_config, &Mplus2_Light_16);

    // epd_main.c の app_main() 関数内に追加
    // フォントテスト
    ESP_LOGI(TAG, "Drawing text test");

    // 日本語文字表示テスト
    uint32_t japanese_chars[] = {
        0x3042, // あ
        0x3044, // い
        0x3046, // う
        0x3048, // え
        0x304A  // お
    };

    int char_x = 50;
    int char_y = 50;

    for (int i = 0; i < sizeof(japanese_chars) / sizeof(japanese_chars[0]); i++)
    {
        int width = epd_text_draw_char(epd, char_x, char_y, japanese_chars[i], &text_config);
        char_x += width + 5; // 5ピクセルの間隔を追加
    }

    // 英数字表示テスト
    char_x = 50;
    char_y = 100;

    for (char c = 'A'; c <= 'Z'; c++)
    {
        int width = epd_text_draw_char(epd, char_x, char_y, c, &text_config);
        char_x += width + 2; // 2ピクセルの間隔を追加
    }

    char_x = 50;
    char_y = 150;

    for (char c = '0'; c <= '9'; c++)
    {
        int width = epd_text_draw_char(epd, char_x, char_y, c, &text_config);
        char_x += width + 2;
    }

    // 太字テスト
    char_x = 50;
    char_y = 200;
    text_config.bold = true;
    text_config.text_color = 0x00; // 黒色で表示

    int width = epd_text_draw_char(epd, char_x, char_y, 'B', &text_config);
    char_x += width + 5;
    width = epd_text_draw_char(epd, char_x, char_y, 0x6F22, &text_config); // '漢'
    char_x += width + 5;

    // 下線テスト
    text_config.bold = false;
    text_config.underline = true;
    width = epd_text_draw_char(epd, char_x, char_y, 'U', &text_config);
    char_x += width + 5;
    width = epd_text_draw_char(epd, char_x, char_y, 0x5B57, &text_config); // '字'

    // 縦書きテスト
    char_x = 300;
    char_y = 50;
    text_config.vertical = true;
    text_config.bold = false;
    text_config.underline = false;
    text_config.text_color = 0x00; // 黒色で表示

    // 縦書きCJK文字
    for (int i = 0; i < sizeof(japanese_chars) / sizeof(japanese_chars[0]); i++)
    {
        int height = epd_text_draw_char(epd, char_x, char_y, japanese_chars[i], &text_config);
        char_y += height + 5; // 縦書きなので次は下に
    }

    // 縦書き英字（回転テスト）
    char_x = 350;
    char_y = 50;
    text_config.rotate_non_cjk = true;

    for (char c = 'A'; c <= 'E'; c++)
    {
        int height = epd_text_draw_char(epd, char_x, char_y, c, &text_config);
        char_y += height + 5;
    }

    // 背景色テスト
    char_x = 400;
    char_y = 50;
    text_config.vertical = false;
    text_config.bg_transparent = false;
    text_config.bg_color = 0x0C;   // 薄いグレー
    text_config.text_color = 0x00; // 黒

    width = epd_text_draw_char(epd, char_x, char_y, 'A', &text_config);
    char_y += 30;
    width = epd_text_draw_char(epd, char_x, char_y, 0x65E5, &text_config); // '日'
    char_y += 30;
    text_config.text_color = 0x0F; // 白
    text_config.bg_color = 0x00;   // 黒背景
    width = epd_text_draw_char(epd, char_x, char_y, 'B', &text_config);
    char_y += 30;
    width = epd_text_draw_char(epd, char_x, char_y, 0x672C, &text_config); // '本'

    // 設定を元に戻す
    text_config.vertical = false;
    text_config.bg_transparent = true;
    text_config.bold = false;
    text_config.underline = false;
    text_config.text_color = 0x00;

    // 画面更新
    ESP_LOGI(TAG, "Updating display");
    epd_wrapper_update_screen(epd, MODE_GC16);
}
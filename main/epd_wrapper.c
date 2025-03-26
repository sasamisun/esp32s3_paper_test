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

bool epd_wrapper_init(EPDWrapper *wrapper)
{
    if (wrapper == NULL)
    {
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
    if (wrapper->framebuffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        epd_deinit(); // 初期化に失敗したらリソース解放
        return false;
    }

    // デフォルト値を設定
    wrapper->is_initialized = true;
    wrapper->is_powered_on = false; // 明示的に電源OFFに設定
    wrapper->rotation = 0;          // デフォルトは0度回転（EPD_ROT_LANDSCAPE）

    ESP_LOGI(TAG, "EPD wrapper initialized successfully");
    return true;
}

void epd_wrapper_deinit(EPDWrapper *wrapper)
{
    if (wrapper == NULL || !wrapper->is_initialized)
    {
        ESP_LOGW(TAG, "EPD wrapper not initialized or already deinitialized");
        return;
    }

    // 電源が入っていたら切る
    if (wrapper->is_powered_on)
    {
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

void epd_wrapper_power_on(EPDWrapper *wrapper)
{
    if (wrapper == NULL || !wrapper->is_initialized)
    {
        ESP_LOGE(TAG, "EPD wrapper not initialized");
        return;
    }

    if (!wrapper->is_powered_on)
    {
        // EPDIYライブラリのパネル電源ON
        ESP_LOGI(TAG, "Powering on the display");
        epd_poweron();
        wrapper->is_powered_on = true;
        // 電源が安定するまで少し待機
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    else
    {
        ESP_LOGW(TAG, "Display is already powered on");
    }
}

void epd_wrapper_power_off(EPDWrapper *wrapper)
{
    if (wrapper == NULL || !wrapper->is_initialized)
    {
        ESP_LOGE(TAG, "EPD wrapper not initialized");
        return;
    }

    if (wrapper->is_powered_on)
    {
        ESP_LOGI(TAG, "Powering off the display");
        epd_poweroff();
        wrapper->is_powered_on = false;
        // 電源OFFが完了するまで少し待機
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    else
    {
        ESP_LOGW(TAG, "Display is already powered off");
    }
}

void epd_wrapper_fill(EPDWrapper *wrapper, uint8_t color)
{
    if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL)
    {
        ESP_LOGE(TAG, "EPD wrapper not properly initialized");
        return;
    }

    // 4ビット/ピクセルの場合、2つのピクセルで1バイトを共有
    // すべてのバイトに同じ値を書き込む
    memset(wrapper->framebuffer, color, EPD_DISPLAY_WIDTH * EPD_DISPLAY_HEIGHT / 2);
    ESP_LOGI(TAG, "Framebuffer filled with color 0x%02x", color);
}

void epd_wrapper_clear_cycles(EPDWrapper *wrapper, int cycles)
{
    if (wrapper == NULL || !wrapper->is_initialized)
    {
        ESP_LOGE(TAG, "EPD wrapper not initialized");
        return;
    }

    if (!wrapper->is_powered_on)
    {
        ESP_LOGW(TAG, "EPD power is off, turning on for clear cycles");
        epd_wrapper_power_on(wrapper);
    }

    ESP_LOGI(TAG, "Starting %d clear cycles", cycles);

    // 最大サイクル数を制限（安全のため）
    if (cycles > 3)
    {
        ESP_LOGW(TAG, "Limiting clear cycles to 3 for safety");
        cycles = 3;
    }

    // まず白で塗りつぶす
    ESP_LOGI(TAG, "Initial fill with white");
    epd_wrapper_fill(wrapper, 0xFF);
    epd_wrapper_update_screen(wrapper, MODE_GC16);
    vTaskDelay(300 / portTICK_PERIOD_MS);

    for (int clear_count = 0; clear_count < cycles; clear_count++)
    {
        ESP_LOGI(TAG, "Clear cycle %d/%d", clear_count + 1, cycles);

        // 安全のため、最初のサイクルでのみ黒塗りを行う
        if (clear_count == 0)
        {
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

void epd_wrapper_update_screen(EPDWrapper *wrapper, enum EpdDrawMode mode)
{
    if (wrapper == NULL || !wrapper->is_initialized)
    {
        ESP_LOGE(TAG, "EPD wrapper not initialized");
        return;
    }

    if (!wrapper->is_powered_on)
    {
        ESP_LOGW(TAG, "EPD power is off, turning on for update");
        epd_wrapper_power_on(wrapper);
    }

    float temperature = epd_ambient_temperature();
    epd_hl_update_screen(&wrapper->hl_state, mode, temperature);
    ESP_LOGI(TAG, "Screen updated with mode %d", mode);
}

void epd_wrapper_draw_circle(EPDWrapper *wrapper, int x, int y, int radius, uint8_t color)
{
    if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL)
    {
        ESP_LOGE(TAG, "EPD wrapper not properly initialized");
        return;
    }

    epd_draw_circle(x, y, radius, color, wrapper->framebuffer);
}

void epd_wrapper_fill_circle(EPDWrapper *wrapper, int x, int y, int radius, uint8_t color)
{
    if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL)
    {
        ESP_LOGE(TAG, "EPD wrapper not properly initialized");
        return;
    }

    epd_fill_circle(x, y, radius, color, wrapper->framebuffer);
}

void epd_wrapper_draw_line(EPDWrapper *wrapper, int x0, int y0, int x1, int y1, uint8_t color)
{
    if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL)
    {
        ESP_LOGE(TAG, "EPD wrapper not properly initialized");
        return;
    }

    epd_draw_line(x0, y0, x1, y1, color, wrapper->framebuffer);
}

void epd_wrapper_draw_rect(EPDWrapper *wrapper, int x, int y, int width, int height, uint8_t color)
{
    if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL)
    {
        ESP_LOGE(TAG, "EPD wrapper not properly initialized");
        return;
    }

    EpdRect rect = {
        .x = x,
        .y = y,
        .width = width,
        .height = height};

    epd_draw_rect(rect, color, wrapper->framebuffer);
}

void epd_wrapper_fill_rect(EPDWrapper *wrapper, int x, int y, int width, int height, uint8_t color)
{
    if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL)
    {
        ESP_LOGE(TAG, "EPD wrapper not properly initialized");
        return;
    }

    EpdRect rect = {
        .x = x,
        .y = y,
        .width = width,
        .height = height};

    epd_fill_rect(rect, color, wrapper->framebuffer);
}

void epd_wrapper_draw_image(EPDWrapper *wrapper, int x, int y, int width, int height, const uint8_t *image_data)
{
    if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL || image_data == NULL)
    {
        ESP_LOGE(TAG, "EPD wrapper not properly initialized or invalid image data");
        return;
    }

    EpdRect image_area = {
        .x = x,
        .y = y,
        .width = width,
        .height = height};

    epd_copy_to_framebuffer(image_area, image_data, wrapper->framebuffer);
}

/**
 * @brief 画像データを特定の角度で回転させる関数
 * @param src_data 元の画像データ
 * @param src_width 元の画像の幅
 * @param src_height 元の画像の高さ
 * @param rotation 回転角度（0:0度, 1:90度, 2:180度, 3:270度）
 * @param dst_data 回転後の画像データを格納するバッファ（事前に確保必要）
 * @return 成功時は0、失敗時は負の値
 *
 * 注意：dst_dataバッファは、回転後の画像サイズに合わせて事前に確保されている必要があります。
 * 90度・270度回転時は幅と高さが入れ替わることに注意してください。
 */
int rotate_image_data(const uint8_t *src_data, int src_width, int src_height, int rotation, uint8_t *dst_data)
{
    if (src_data == NULL || dst_data == NULL)
    {
        return -1;
    }

    // 4ビット/ピクセルのフォーマットで1バイトが2ピクセル分
    int src_width_bytes = (src_width + 1) / 2; // 奇数幅の場合も考慮
    int dst_width, dst_width_bytes;

    // 回転後のサイズを計算
    if (rotation == 1 || rotation == 3)
    {
        // 90度・270度回転は幅と高さが入れ替わる
        dst_width = src_height;
    }
    else
    {
        // 0度・180度回転はサイズはそのまま
        dst_width = src_width;
    }

    dst_width_bytes = (dst_width + 1) / 2; // 奇数幅の場合も考慮

    // 回転処理
    switch (rotation)
    {
    case 0: // 0度（変更なし）
        memcpy(dst_data, src_data, src_width_bytes * src_height);
        break;

    case 1: // 90度時計回り
        for (int y = 0; y < src_height; y++)
        {
            for (int x = 0; x < src_width; x++)
            {
                // 元の位置からピクセル値を取得
                uint8_t src_value;
                int src_index = y * src_width_bytes + x / 2;
                if (x % 2 == 0)
                {
                    src_value = src_data[src_index] & 0x0F; // 下位4ビット
                }
                else
                {
                    src_value = (src_data[src_index] & 0xF0) >> 4; // 上位4ビット
                }

                // 回転後の位置を計算（時計回り90度）
                int new_x = src_height - 1 - y;
                int new_y = x;

                // 新しい位置に設定
                int dst_index = new_y * dst_width_bytes + new_x / 2;
                if (new_x % 2 == 0)
                {
                    dst_data[dst_index] = (dst_data[dst_index] & 0xF0) | src_value;
                }
                else
                {
                    dst_data[dst_index] = (dst_data[dst_index] & 0x0F) | (src_value << 4);
                }
            }
        }
        break;

    case 2: // 180度
        for (int y = 0; y < src_height; y++)
        {
            for (int x = 0; x < src_width; x++)
            {
                // 元の位置からピクセル値を取得
                uint8_t src_value;
                int src_index = y * src_width_bytes + x / 2;
                if (x % 2 == 0)
                {
                    src_value = src_data[src_index] & 0x0F; // 下位4ビット
                }
                else
                {
                    src_value = (src_data[src_index] & 0xF0) >> 4; // 上位4ビット
                }

                // 回転後の位置を計算（180度）
                int new_x = src_width - 1 - x;
                int new_y = src_height - 1 - y;

                // 新しい位置に設定
                int dst_index = new_y * dst_width_bytes + new_x / 2;
                if (new_x % 2 == 0)
                {
                    dst_data[dst_index] = (dst_data[dst_index] & 0xF0) | src_value;
                }
                else
                {
                    dst_data[dst_index] = (dst_data[dst_index] & 0x0F) | (src_value << 4);
                }
            }
        }
        break;

    case 3: // 270度時計回り
        for (int y = 0; y < src_height; y++)
        {
            for (int x = 0; x < src_width; x++)
            {
                // 元の位置からピクセル値を取得
                uint8_t src_value;
                int src_index = y * src_width_bytes + x / 2;
                if (x % 2 == 0)
                {
                    src_value = src_data[src_index] & 0x0F; // 下位4ビット
                }
                else
                {
                    src_value = (src_data[src_index] & 0xF0) >> 4; // 上位4ビット
                }

                // 回転後の位置を計算（反時計回り90度＝時計回り270度）
                int new_x = y;
                int new_y = src_width - 1 - x;

                // 新しい位置に設定
                int dst_index = new_y * dst_width_bytes + new_x / 2;
                if (new_x % 2 == 0)
                {
                    dst_data[dst_index] = (dst_data[dst_index] & 0xF0) | src_value;
                }
                else
                {
                    dst_data[dst_index] = (dst_data[dst_index] & 0x0F) | (src_value << 4);
                }
            }
        }
        break;

    default:
        return -2; // 不正な回転値
    }

    return 0;
}

/**
 * @brief 画像データを回転させて描画する関数（透明色対応版）
 * @param wrapper EPDラッパー構造体へのポインタ
 * @param x 左上X座標
 * @param y 左上Y座標
 * @param width 画像の幅
 * @param height 画像の高さ
 * @param image_data 画像データ
 * @param rotate_image 画像自体を回転させるかどうか（true:画像を回転, false:座標のみ回転）
 * @param use_transparency 透明処理を有効にするかどうか
 * @param transparent_color 透明とする色（0-15の値）
 */
void epd_wrapper_draw_rotated_image_with_transparency(EPDWrapper *wrapper,
                                                      int x, int y,
                                                      int width, int height,
                                                      const uint8_t *image_data,
                                                      bool rotate_image,
                                                      bool use_transparency,
                                                      uint8_t transparent_color)
{
    if (wrapper == NULL || !wrapper->is_initialized ||
        wrapper->framebuffer == NULL || image_data == NULL)
    {
        ESP_LOGE(TAG, "EPD wrapper not properly initialized or invalid image data");
        return;
    }

    // 透明色を4ビット値（0-15）に制限
    transparent_color &= 0x0F;

    // 透明色を8ビット値に変換（epdiyの関数で使用するため）
    //uint8_t transparent_color_8bit = transparent_color << 4;

    // 現在の回転を取得
    int rotation = wrapper->rotation;

    // 回転が設定されていなければ、標準の描画関数を使用（透明処理対応）
    if (rotation == 0 || !rotate_image)
    {
        // 透明処理を使用しない場合は通常のコピー
        if (!use_transparency)
        {
            EpdRect image_area = {
                .x = x,
                .y = y,
                .width = width,
                .height = height};
            epd_copy_to_framebuffer(image_area, image_data, wrapper->framebuffer);
            return;
        }

        // 透明処理を使用する場合はピクセルごとに判断
        for (int img_y = 0; img_y < height; img_y++)
        {
            for (int img_x = 0; img_x < width; img_x++)
            {
                // 画像のピクセル位置 - ピクセル単位で計算
                int img_pos = img_y * width + img_x;
                int img_byte_pos = img_pos / 2;
                uint8_t img_pixel;

                // 4ビット/ピクセルのデータを取得
                if (img_pos % 2 == 0)
                {
                    // 偶数ピクセルは下位4ビット
                    img_pixel = image_data[img_byte_pos] & 0x0F;
                }
                else
                {
                    // 奇数ピクセルは上位4ビット
                    img_pixel = (image_data[img_byte_pos] & 0xF0) >> 4;
                }

                // 透明色でない場合のみ描画
                if (!use_transparency || img_pixel != transparent_color)
                {
                    int dx = x + img_x;
                    int dy = y + img_y;

                    // EPDIYライブラリの描画関数を使用して回転を正しく処理
                    // epdiyは8ビット値を期待するので、4ビット値を8ビット形式に変換
                    uint8_t color_8bit = (img_pixel << 4) | img_pixel; // 4ビット値を8ビットに拡張
                    epd_draw_pixel(dx, dy, color_8bit, wrapper->framebuffer);
                }
            }
        }
        return;
    }

    // 画像を回転させる場合
    // 透明処理を使用しない場合は通常のコピー
    if (!use_transparency)
    {

        // 回転後のサイズを計算
        int rotated_width, rotated_height;
        if (rotation == 1 || rotation == 3)
        {
            // 90度・270度回転は幅と高さが入れ替わる
            rotated_width = height;
            rotated_height = width;
        }
        else
        {
            // 0度・180度回転はサイズそのまま
            rotated_width = width;
            rotated_height = height;
        }

        // 回転後画像用のバッファを確保
        int rotated_bytes = ((rotated_width + 1) / 2) * rotated_height; // 4bit/pixelのバイト数
        uint8_t *rotated_data = heap_caps_malloc(rotated_bytes, MALLOC_CAP_8BIT);
        if (rotated_data == NULL)
        {
            ESP_LOGE(TAG, "Failed to allocate memory for rotated image");
            return;
        }

        // バッファを0で初期化（重要：回転処理でビット演算を行うため）
        memset(rotated_data, 0, rotated_bytes);

        // 画像データを回転
        if (rotate_image_data(image_data, width, height, rotation, rotated_data) != 0)
        {
            ESP_LOGE(TAG, "Failed to rotate image data");
            heap_caps_free(rotated_data);
            return;
        }

        // 回転後の位置を計算（画像は既に回転済みなので座標だけ調整）
        int adjusted_x, adjusted_y;
        switch (rotation)
        {
        case 1: // 90度回転
            adjusted_x = EPD_DISPLAY_WIDTH - y - height;
            adjusted_y = x;
            break;
        case 2: // 180度回転
            adjusted_x = EPD_DISPLAY_WIDTH - x - width;
            adjusted_y = EPD_DISPLAY_HEIGHT - y - height;
            break;
        case 3: // 270度回転
            adjusted_x = y;
            adjusted_y = EPD_DISPLAY_HEIGHT - x - width;
            break;
        default: // 0度回転（ここには来ないはず）
            adjusted_x = x;
            adjusted_y = y;
            break;
        }
        // 回転済み画像を描画
        EpdRect image_area = {
            .x = adjusted_x,
            .y = adjusted_y,
            .width = rotated_width,
            .height = rotated_height};

        epd_copy_to_framebuffer(image_area, rotated_data, wrapper->framebuffer);

        // 一時バッファを解放
        heap_caps_free(rotated_data);
    }
    else
    {
        // 透明処理を使用する場合はピクセルごとに判断
        for (int img_y = 0; img_y < height; img_y++)
        {
            for (int img_x = 0; img_x < width; img_x++)
            {
                // 画像のピクセル位置を計算
                int img_row_offset = img_y * ((width + 1) / 2); // 4bit/pixelのバイト数で1行のオフセットを計算
                int img_byte_pos = img_row_offset + (img_x / 2);
                uint8_t img_pixel;

                // 4ビット/ピクセルのデータを取得
                if (img_x % 2 == 0)
                {
                    // 偶数ピクセルは下位4ビット
                    img_pixel = image_data[img_byte_pos] & 0x0F;
                }
                else
                {
                    // 奇数ピクセルは上位4ビット
                    img_pixel = (image_data[img_byte_pos] & 0xF0) >> 4;
                }

                // 透明色でない場合のみ描画
                if (img_pixel != transparent_color)
                {
                    int dx = x + img_x;
                    int dy = y + img_y;

                    // epd_draw_pixelは回転を考慮しなくても良い
                    uint8_t color_8bit = img_pixel << 4 | img_pixel; // 4ビット値を8ビットに拡張
                    epd_draw_pixel(dx, dy, color_8bit, wrapper->framebuffer);
                }
            }
        }
    }
}

/**
 * @brief 元の関数をオーバーロードして、新しい透明処理機能対応版を提供
 * @param wrapper EPDラッパー構造体へのポインタ
 * @param x 左上X座標
 * @param y 左上Y座標
 * @param width 画像の幅
 * @param height 画像の高さ
 * @param image_data 画像データ
 * @param rotate_image 画像自体を回転させるかどうか（true:画像を回転, false:座標のみ回転）
 */
void epd_wrapper_draw_rotated_image(EPDWrapper *wrapper, int x, int y,
                                    int width, int height,
                                    const uint8_t *image_data,
                                    bool rotate_image)
{
    // 透明処理なしで新しい関数を呼び出す
    epd_wrapper_draw_rotated_image_with_transparency(wrapper, x, y, width, height,
                                                     image_data, rotate_image,
                                                     false, 0);
}

/**
 * @brief 透明色を指定して画像を描画する（後方互換性のためのヘルパー関数）
 * @param wrapper EPDラッパー構造体へのポインタ
 * @param x 左上X座標
 * @param y 左上Y座標
 * @param width 画像の幅
 * @param height 画像の高さ
 * @param image_data 画像データ
 * @param rotate_image 画像自体を回転させるかどうか（true:画像を回転, false:座標のみ回転）
 * @param transparent_color 透明とする色（0-15の値）
 */
void epd_wrapper_draw_transparent_image(EPDWrapper *wrapper, int x, int y,
                                        int width, int height,
                                        const uint8_t *image_data,
                                        bool rotate_image,
                                        uint8_t transparent_color)
{
    // 透明処理ありで新しい関数を呼び出す
    epd_wrapper_draw_rotated_image_with_transparency(wrapper, x, y, width, height,
                                                     image_data, rotate_image,
                                                     true, transparent_color);
}

void epd_wrapper_draw_grayscale_test(EPDWrapper *wrapper, int x, int y, int width, int height)
{
    if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL)
    {
        ESP_LOGE(TAG, "EPD wrapper not properly initialized");
        return;
    }

    // グレースケールパターンを描画（16段階）
    for (int i = 0; i < 16; i++)
    {
        int pattern_x = x + i * (width / 16);
        int pattern_width = width / 16;

        for (int dy = 0; dy < height; dy++)
        {
            for (int dx = 0; dx < pattern_width; dx++)
            {
                int pos = (y + dy) * EPD_DISPLAY_WIDTH + (pattern_x + dx);
                int byte_pos = pos / 2;

                if (pos % 2 == 0)
                {
                    // 偶数ピクセルは下位4ビット
                    wrapper->framebuffer[byte_pos] = (wrapper->framebuffer[byte_pos] & 0xF0) | i;
                }
                else
                {
                    // 奇数ピクセルは上位4ビット
                    wrapper->framebuffer[byte_pos] = (wrapper->framebuffer[byte_pos] & 0x0F) | (i << 4);
                }
            }
        }
    }
}

uint8_t *epd_wrapper_get_framebuffer(EPDWrapper *wrapper)
{
    if (wrapper == NULL || !wrapper->is_initialized)
    {
        ESP_LOGE(TAG, "EPD wrapper not initialized");
        return NULL;
    }

    return wrapper->framebuffer;
}

bool epd_wrapper_set_rotation(EPDWrapper *wrapper, int rotation)
{
    if (wrapper == NULL || !wrapper->is_initialized)
    {
        ESP_LOGE(TAG, "EPD wrapper not initialized");
        return false;
    }

    // 0〜3の値であることを確認
    if (rotation < 0 || rotation > 3)
    {
        ESP_LOGE(TAG, "Invalid rotation value: %d (must be 0-3)", rotation);
        return false;
    }

    // 回転設定を保存
    wrapper->rotation = rotation;

    // epdiyライブラリの回転モードに変換
    enum EpdRotation epd_rotation;
    switch (rotation)
    {
    case 0:
        epd_rotation = EPD_ROT_LANDSCAPE; // 0度回転
        break;
    case 1:
        epd_rotation = EPD_ROT_PORTRAIT; // 90度回転
        break;
    case 2:
        epd_rotation = EPD_ROT_INVERTED_LANDSCAPE; // 180度回転
        break;
    case 3:
        epd_rotation = EPD_ROT_INVERTED_PORTRAIT; // 270度回転
        break;
    default:
        epd_rotation = EPD_ROT_PORTRAIT; // デフォルト
        break;
    }

    // epdiyライブラリに回転を設定
    epd_set_rotation(epd_rotation);

    ESP_LOGI(TAG, "Display rotation set to %d (%d degrees)", rotation, rotation * 90);
    return true;
}

int epd_wrapper_get_rotation(EPDWrapper *wrapper)
{
    if (wrapper == NULL || !wrapper->is_initialized)
    {
        ESP_LOGE(TAG, "EPD wrapper not initialized");
        return -1;
    }

    return wrapper->rotation;
}

int epd_wrapper_get_width(EPDWrapper *wrapper)
{
    if (wrapper == NULL || !wrapper->is_initialized)
    {
        ESP_LOGE(TAG, "EPD wrapper not initialized");
        return 0;
    }

    // 90度または270度回転の場合は幅と高さが入れ替わる
    if (wrapper->rotation == 1 || wrapper->rotation == 3)
    {
        return EPD_DISPLAY_HEIGHT;
    }
    else
    {
        return EPD_DISPLAY_WIDTH;
    }
}

int epd_wrapper_get_height(EPDWrapper *wrapper)
{
    if (wrapper == NULL || !wrapper->is_initialized)
    {
        ESP_LOGE(TAG, "EPD wrapper not initialized");
        return 0;
    }

    // 90度または270度回転の場合は幅と高さが入れ替わる
    if (wrapper->rotation == 1 || wrapper->rotation == 3)
    {
        return EPD_DISPLAY_WIDTH;
    }
    else
    {
        return EPD_DISPLAY_HEIGHT;
    }
}

/**
 * @brief 1ピクセルを描画する
 * @param wrapper EPDラッパー構造体へのポインタ
 * @param x X座標
 * @param y Y座標
 * @param color 色（0-15のグレースケール）
 */
void epd_wrapper_draw_pixel(EPDWrapper *wrapper, int x, int y, uint8_t color) {
    if (wrapper == NULL || !wrapper->is_initialized || wrapper->framebuffer == NULL) {
        ESP_LOGE(TAG, "EPD wrapper not properly initialized");
        return;
    }

    // 正確なピクセル位置を計算
    int pos = y * EPD_DISPLAY_WIDTH + x;
    int byte_pos = pos / 2;
    
    // 4ビット/ピクセルのバッファでは、1バイトに2つのピクセルが格納される
    if (pos % 2 == 0) {
        // 偶数ピクセル（下位4ビット）
        wrapper->framebuffer[byte_pos] = (wrapper->framebuffer[byte_pos] & 0xF0) | (color & 0x0F);
    } else {
        // 奇数ピクセル（上位4ビット）
        wrapper->framebuffer[byte_pos] = (wrapper->framebuffer[byte_pos] & 0x0F) | ((color & 0x0F) << 4);
    }
}

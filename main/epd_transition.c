/**
 * @file epd_transition.c
 * @brief 電子ペーパーディスプレイのトランジションエフェクト機能の実装
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "epd_wrapper.h"
#include "epd_transition.h"

static const char *TAG = "epd_transition";

// トランジション機能の初期化
bool epd_transition_init(EPDWrapper *wrapper, EPDTransition *transition, int steps)
{
    if (wrapper == NULL || transition == NULL || !wrapper->is_initialized)
    {
        ESP_LOGE(TAG, "Invalid parameters for initialization");
        return false;
    }

    // 有効なステップ数かチェック (2, 4, 8, 16のみ)
    if (steps != 2 && steps != 4 && steps != 8 && steps != 16)
    {
        ESP_LOGE(TAG, "Invalid step count: %d (must be 2, 4, 8 or 16)", steps);
        return false;
    }

    // 構造体を初期化
    memset(transition, 0, sizeof(EPDTransition));

    // フレームバッファのサイズを計算
    // 4bit/pixel なので 2pixelで1バイト
    size_t framebuffer_size = EPD_DISPLAY_WIDTH * EPD_DISPLAY_HEIGHT / 2;

    // PSRAMにフレームバッファBを確保
    transition->framebuffer_next = heap_caps_malloc(framebuffer_size, MALLOC_CAP_SPIRAM);
    if (transition->framebuffer_next == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate next framebuffer in PSRAM");
        epd_transition_deinit(wrapper, transition);
        return false;
    }

    // バッファを白(0xFF)で初期化
    memset(transition->framebuffer_next, 0xFF, framebuffer_size);

    // パラメータを設定
    transition->steps = steps;
    transition->current_step = 0;
    transition->is_active = false;
    transition->update_mode = MODE_GC16; // デフォルトモード

    ESP_LOGI(TAG, "Transition initialized with %d steps", steps);
    return true;
}

/**
 * @brief グレースケールマスクを生成する内部関数
 * @param transition トランジション構造体へのポインタ
 * @param type トランジションの種類
 * @return 成功したかどうか
 */
static bool generate_transition_mask(EPDTransition *transition, TransitionType type)
{
    if (transition == NULL)
    {
        return false;
    }

    // マスクの幅と高さを設定
    transition->transition_width = EPD_DISPLAY_WIDTH;
    transition->transition_height = EPD_DISPLAY_HEIGHT;

    // マスクのサイズを計算 (4bit/pixel なので 2pixelで1バイト)
    size_t mask_size = transition->transition_width * transition->transition_height / 2;

    // すでにマスクが存在する場合は解放
    if (transition->transition_mask != NULL)
    {
        heap_caps_free(transition->transition_mask);
        transition->transition_mask = NULL;
    }

    // PSRAMにマスク用メモリを確保
    transition->transition_mask = heap_caps_malloc(mask_size, MALLOC_CAP_SPIRAM);
    if (transition->transition_mask == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for transition mask");
        return false;
    }

    // マスクの種類に応じた値を設定
    switch (type)
    {
    case TRANSITION_FADE:
        // フェード効果用のマスク (均一な値)
        memset(transition->transition_mask, 0xFF, mask_size);
        break;

    case TRANSITION_SLIDE_LEFT:
        // 左からスライド用のマスク
        for (int y = 0; y < transition->transition_height; y++)
        {
            for (int x = 0; x < transition->transition_width; x++)
            {
                // x座標に応じてグラデーション値を設定 (左から右へ 0→15)
                uint8_t value = (x * 16) / transition->transition_width;
                if (value > 15)
                    value = 15;

                // 4bit/pixelフォーマットなので、2ピクセルで1バイト
                int pos = y * transition->transition_width + x;
                int byte_pos = pos / 2;

                if (pos % 2 == 0)
                {
                    // 偶数ピクセルは下位4ビット
                    transition->transition_mask[byte_pos] =
                        (transition->transition_mask[byte_pos] & 0xF0) | value;
                }
                else
                {
                    // 奇数ピクセルは上位4ビット
                    transition->transition_mask[byte_pos] =
                        (transition->transition_mask[byte_pos] & 0x0F) | (value << 4);
                }
            }
        }
        break;

    case TRANSITION_SLIDE_RIGHT:
        // 右からスライド用のマスク
        for (int y = 0; y < transition->transition_height; y++)
        {
            for (int x = 0; x < transition->transition_width; x++)
            {
                // x座標に応じてグラデーション値を設定 (右から左へ 0→15)
                uint8_t value = ((transition->transition_width - 1 - x) * 16) / transition->transition_width;
                if (value > 15)
                    value = 15;

                // 4bit/pixelフォーマットなので、2ピクセルで1バイト
                int pos = y * transition->transition_width + x;
                int byte_pos = pos / 2;

                if (pos % 2 == 0)
                {
                    // 偶数ピクセルは下位4ビット
                    transition->transition_mask[byte_pos] =
                        (transition->transition_mask[byte_pos] & 0xF0) | value;
                }
                else
                {
                    // 奇数ピクセルは上位4ビット
                    transition->transition_mask[byte_pos] =
                        (transition->transition_mask[byte_pos] & 0x0F) | (value << 4);
                }
            }
        }
        break;

    case TRANSITION_SLIDE_UP:
        // 上からスライド用のマスク
        for (int y = 0; y < transition->transition_height; y++)
        {
            for (int x = 0; x < transition->transition_width; x++)
            {
                // y座標に応じてグラデーション値を設定 (上から下へ 0→15)
                uint8_t value = (y * 16) / transition->transition_height;
                if (value > 15)
                    value = 15;

                // 4bit/pixelフォーマットなので、2ピクセルで1バイト
                int pos = y * transition->transition_width + x;
                int byte_pos = pos / 2;

                if (pos % 2 == 0)
                {
                    // 偶数ピクセルは下位4ビット
                    transition->transition_mask[byte_pos] =
                        (transition->transition_mask[byte_pos] & 0xF0) | value;
                }
                else
                {
                    // 奇数ピクセルは上位4ビット
                    transition->transition_mask[byte_pos] =
                        (transition->transition_mask[byte_pos] & 0x0F) | (value << 4);
                }
            }
        }
        break;

    case TRANSITION_SLIDE_DOWN:
        // 下からスライド用のマスク
        for (int y = 0; y < transition->transition_height; y++)
        {
            for (int x = 0; x < transition->transition_width; x++)
            {
                // y座標に応じてグラデーション値を設定 (下から上へ 0→15)
                uint8_t value = ((transition->transition_height - 1 - y) * 16) / transition->transition_height;
                if (value > 15)
                    value = 15;

                // 4bit/pixelフォーマットなので、2ピクセルで1バイト
                int pos = y * transition->transition_width + x;
                int byte_pos = pos / 2;

                if (pos % 2 == 0)
                {
                    // 偶数ピクセルは下位4ビット
                    transition->transition_mask[byte_pos] =
                        (transition->transition_mask[byte_pos] & 0xF0) | value;
                }
                else
                {
                    // 奇数ピクセルは上位4ビット
                    transition->transition_mask[byte_pos] =
                        (transition->transition_mask[byte_pos] & 0x0F) | (value << 4);
                }
            }
        }
        break;

    case TRANSITION_WIPE:
        // ワイプ効果（対角線に沿ったグラデーション）
        for (int y = 0; y < transition->transition_height; y++)
        {
            for (int x = 0; x < transition->transition_width; x++)
            {
                // 対角線までの距離に応じた値
                float diagonal_pos = ((float)x / transition->transition_width) +
                                     ((float)y / transition->transition_height);
                uint8_t value = (uint8_t)(diagonal_pos * 8.0); // 0-15の範囲にスケール
                if (value > 15)
                    value = 15;

                // 4bit/pixelフォーマットなので、2ピクセルで1バイト
                int pos = y * transition->transition_width + x;
                int byte_pos = pos / 2;

                if (pos % 2 == 0)
                {
                    // 偶数ピクセルは下位4ビット
                    transition->transition_mask[byte_pos] =
                        (transition->transition_mask[byte_pos] & 0xF0) | value;
                }
                else
                {
                    // 奇数ピクセルは上位4ビット
                    transition->transition_mask[byte_pos] =
                        (transition->transition_mask[byte_pos] & 0x0F) | (value << 4);
                }
            }
        }
        break;

    case TRANSITION_CUSTOM:
        // カスタムマスクは別の関数で設定するため、ここでは何もしない
        memset(transition->transition_mask, 0, mask_size);
        break;

    default:
        ESP_LOGE(TAG, "Unsupported transition type");
        heap_caps_free(transition->transition_mask);
        transition->transition_mask = NULL;
        return false;
    }

    ESP_LOGI(TAG, "Generated transition mask for type %d", type);
    return true;
}

// トランジションの準備を行う
bool epd_transition_prepare(EPDWrapper *wrapper, EPDTransition *transition,
                            TransitionType type, enum EpdDrawMode update_mode)
{
    if (wrapper == NULL || transition == NULL || !wrapper->is_initialized ||
        transition->framebuffer_next == NULL)
    {
        ESP_LOGE(TAG, "Invalid parameters for transition preparation");
        return false;
    }

    // すでにトランジションが進行中ならキャンセル
    if (transition->is_active)
    {
        ESP_LOGW(TAG, "Cancelling in-progress transition");
        transition->is_active = false;
    }

    // トランジションタイプを設定
    transition->type = type;
    transition->update_mode = update_mode;
    transition->current_step = 0;

    // マスクを生成
    if (!generate_transition_mask(transition, type))
    {
        ESP_LOGE(TAG, "Failed to generate transition mask");
        return false;
    }

    // トランジションを開始
    transition->is_active = true;

    ESP_LOGI(TAG, "Transition prepared with type %d and %d steps", type, transition->steps);
    return true;
}

// カスタムマスク画像でのトランジション準備
bool epd_transition_prepare_with_mask(EPDWrapper *wrapper, EPDTransition *transition,
                                      const uint8_t *mask_data, int width, int height,
                                      enum EpdDrawMode update_mode)
{
    if (wrapper == NULL || transition == NULL || !wrapper->is_initialized ||
        transition->framebuffer_next == NULL || mask_data == NULL)
    {
        ESP_LOGE(TAG, "Invalid parameters for custom mask transition");
        return false;
    }

    // すでにトランジションが進行中ならキャンセル
    if (transition->is_active)
    {
        ESP_LOGW(TAG, "Cancelling in-progress transition");
        transition->is_active = false;
    }

    // パラメータを設定
    transition->type = TRANSITION_CUSTOM;
    transition->update_mode = update_mode;
    transition->current_step = 0;
    transition->transition_width = width;
    transition->transition_height = height;

    // マスク用メモリを確保
    size_t mask_size = width * height / 2; // 4bit/pixelなので

    // すでにマスクが存在する場合は解放
    if (transition->transition_mask != NULL)
    {
        heap_caps_free(transition->transition_mask);
        transition->transition_mask = NULL;
    }

    // PSRAMにマスク用メモリを確保
    transition->transition_mask = heap_caps_malloc(mask_size, MALLOC_CAP_SPIRAM);
    if (transition->transition_mask == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for custom transition mask");
        return false;
    }

    // マスクデータをコピー
    memcpy(transition->transition_mask, mask_data, mask_size);

    // トランジションを開始
    transition->is_active = true;

    ESP_LOGI(TAG, "Custom mask transition prepared with dimensions %dx%d", width, height);
    return true;
}

// 次画面のフレームバッファを取得する
uint8_t *epd_transition_get_next_framebuffer(EPDTransition *transition)
{
    if (transition == NULL || transition->framebuffer_next == NULL)
    {
        ESP_LOGE(TAG, "Transition not initialized");
        return NULL;
    }

    return transition->framebuffer_next;
}

/**
 * @brief 現在のステップのしきい値を取得する内部関数
 * @param transition トランジション構造体へのポインタ
 * @return 現在のステップのしきい値（0-15）
 */
static uint8_t get_step_threshold(EPDTransition *transition)
{
    if (transition == NULL || transition->current_step >= transition->steps)
    {
        return 0;
    }

    // 16ステップ以外の場合、非線形にマッピング
    switch (transition->steps)
    {
    case 2:
        // 2ステップの場合： [0, 15]
        return transition->current_step * 15;

    case 4:
        // 4ステップの場合： [0, 5, 10, 15]
        return transition->current_step * 5;

    case 8:
        // 8ステップの場合： [0, 2, 4, 6, 8, 10, 12, 14]
        return transition->current_step * 2;

    case 16:
        // 16ステップの場合： [0, 1, 2, ..., 15]
        return transition->current_step;

    default:
        return 0;
    }
}

// トランジションのステップを実行する
bool epd_transition_step(EPDWrapper *wrapper, EPDTransition *transition)
{
    if (wrapper == NULL || transition == NULL || !wrapper->is_initialized ||
        transition->framebuffer_next == NULL || transition->transition_mask == NULL)
    {
        ESP_LOGE(TAG, "Invalid parameters for transition step");
        return false;
    }

    // トランジションが非アクティブか、すべてのステップが完了した場合
    if (!transition->is_active || transition->current_step >= transition->steps)
    {
        transition->is_active = false;
        ESP_LOGI(TAG, "Transition already completed");
        return false;
    }

    // 現在のステップに対応するしきい値を取得 (0-15)
    uint8_t threshold = get_step_threshold(transition);
    ESP_LOGI(TAG, "Transition step %d/%d with threshold %d",
             transition->current_step + 1, transition->steps, threshold);

    // フレームバッファのサイズを計算
    size_t framebuffer_size = EPD_DISPLAY_WIDTH * EPD_DISPLAY_HEIGHT / 2;

    // 単純化アプローチ：
    // ステップごとに画面の一部分を更新する代わりに、マスク値に基づいて
    // 特定の閾値以下のピクセルを全て更新

    // 実際の処理で使用するしきい値を計算
    // ステップ数に応じて閾値を計算し、その閾値以下のマスク値を持つピクセルを更新
    int step_value = 16 / transition->steps;
    uint8_t current_threshold = (transition->current_step + 1) * step_value - 1;
    if (current_threshold > 15)
        current_threshold = 15;

    ESP_LOGI(TAG, "Using threshold value: %d for step %d",
             current_threshold, transition->current_step + 1);

    // マスクに基づいて、framebuffer_nextからframebufferへコピー
    for (int y = 0; y < EPD_DISPLAY_HEIGHT; y++)
    {
        for (int x = 0; x < EPD_DISPLAY_WIDTH; x++)
        {
            // ピクセル位置を計算
            int pos = y * EPD_DISPLAY_WIDTH + x;
            int byte_pos = pos / 2;
            uint8_t mask_value;

            // マスクが画面と同じサイズでなければスケーリング
            if (transition->transition_width == EPD_DISPLAY_WIDTH &&
                transition->transition_height == EPD_DISPLAY_HEIGHT)
            {
                // マスクが画面と同じサイズの場合、直接値を取得
                if (pos % 2 == 0)
                {
                    mask_value = transition->transition_mask[byte_pos] & 0x0F;
                }
                else
                {
                    mask_value = (transition->transition_mask[byte_pos] & 0xF0) >> 4;
                }
            }
            else
            {
                // マスクサイズが異なる場合はスケーリング
                int mask_x = (x * transition->transition_width) / EPD_DISPLAY_WIDTH;
                int mask_y = (y * transition->transition_height) / EPD_DISPLAY_HEIGHT;
                int mask_pos = mask_y * transition->transition_width + mask_x;
                int mask_byte_pos = mask_pos / 2;

                if (mask_pos % 2 == 0)
                {
                    mask_value = transition->transition_mask[mask_byte_pos] & 0x0F;
                }
                else
                {
                    mask_value = (transition->transition_mask[mask_byte_pos] & 0xF0) >> 4;
                }
            }

            // 現在のステップの閾値以下のマスク値を持つピクセルのみ更新
            if (mask_value <= current_threshold)
            {
                uint8_t pixel;
                if (pos % 2 == 0)
                {
                    pixel = transition->framebuffer_next[byte_pos] & 0x0F;
                    wrapper->framebuffer[byte_pos] = (wrapper->framebuffer[byte_pos] & 0xF0) | pixel;
                }
                else
                {
                    pixel = (transition->framebuffer_next[byte_pos] & 0xF0);
                    wrapper->framebuffer[byte_pos] = (wrapper->framebuffer[byte_pos] & 0x0F) | pixel;
                }
            }
        }
    }

    // フレームバッファを表示
    float temperature = epd_ambient_temperature();
    epd_hl_update_screen(&wrapper->hl_state, transition->update_mode, temperature);

    // 次のステップへ
    transition->current_step++;

    // すべてのステップが完了したら非アクティブにする
    if (transition->current_step >= transition->steps)
    {
        // 最後のステップでは、完全に次のフレームバッファの内容に置き換える
        // （念のため、未更新領域がないように）
        if (transition->current_step == transition->steps)
        {
            memcpy(wrapper->framebuffer, transition->framebuffer_next, framebuffer_size);
            float temperature = epd_ambient_temperature();
            epd_hl_update_screen(&wrapper->hl_state, transition->update_mode, temperature);
        }

        transition->is_active = false;
        ESP_LOGI(TAG, "Transition completed");
    }

    return true;
}

// トランジションを完了する (一度に残りのステップを実行)
bool epd_transition_complete(EPDWrapper *wrapper, EPDTransition *transition)
{
    if (wrapper == NULL || transition == NULL || !wrapper->is_initialized ||
        transition->framebuffer_next == NULL)
    {
        ESP_LOGE(TAG, "Invalid parameters for transition completion");
        return false;
    }

    // すでに完了しているか非アクティブな場合
    if (!transition->is_active || transition->current_step >= transition->steps)
    {
        transition->is_active = false;
        ESP_LOGW(TAG, "Transition already completed");
        return false;
    }

    ESP_LOGI(TAG, "Completing transition (skipping %d remaining steps)",
             transition->steps - transition->current_step);

    // フレームバッファのサイズを計算
    size_t framebuffer_size = EPD_DISPLAY_WIDTH * EPD_DISPLAY_HEIGHT / 2;

    // 最終的に次のフレームバッファの内容を現在のフレームバッファにコピー
    memcpy(wrapper->framebuffer, transition->framebuffer_next, framebuffer_size);

    // 画面を更新
    float temperature = epd_ambient_temperature();
    epd_hl_update_screen(&wrapper->hl_state, transition->update_mode, temperature);

    // トランジションを完了
    transition->current_step = transition->steps;
    transition->is_active = false;

    ESP_LOGI(TAG, "Transition force-completed");
    return true;
}

// トランジションリソースを解放する
void epd_transition_deinit(EPDWrapper *wrapper, EPDTransition *transition)
{
    if (transition == NULL)
    {
        return;
    }

    // 進行中のトランジションを終了
    if (transition->is_active && wrapper != NULL && wrapper->is_initialized)
    {
        ESP_LOGW(TAG, "Cancelling active transition during deinit");
        transition->is_active = false;
    }

    // トランジションマスクを解放
    if (transition->transition_mask != NULL)
    {
        heap_caps_free(transition->transition_mask);
        transition->transition_mask = NULL;
    }

    // 次のフレームバッファを解放
    if (transition->framebuffer_next != NULL)
    {
        heap_caps_free(transition->framebuffer_next);
        transition->framebuffer_next = NULL;
    }

    // 残りのパラメータをリセット
    transition->transition_width = 0;
    transition->transition_height = 0;
    transition->steps = 0;
    transition->current_step = 0;
    transition->is_active = false;

    ESP_LOGI(TAG, "Transition resources deallocated");
}
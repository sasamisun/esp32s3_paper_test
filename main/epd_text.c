/**
 * @file epd_text.c
 * @brief 電子ペーパーディスプレイ向けテキスト表示機能の実装
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "epd_text.h"

static const char *TAG = "epd_text";

// 行頭禁止文字（句読点や閉じ括弧など）
const uint32_t EPD_TEXT_NO_START_CHARS[] = {
    0x3001, // 、
    0x3002, // 。
    0xFF0C, // ，
    0xFF0E, // ．
    0xFF1A, // ：
    0xFF1B, // ；
    0xFF09, // ）
    0x3009, // 〉
    0x300B, // 》
    0x300D, // 」
    0x300F, // 』
    0x3011, // 】
    0xFF09, // ）
    0xFF5D, // ｝
    0xFF60, // ｠
    0x3015, // 〕
    0x2019, // '
    0x201D, // "
    0x3017, // 〗
    0x3019, // 〙
    0x301B, // 〛
    0xFF3D, // ］
    0xFF5D  // ｝
};
const int EPD_TEXT_NO_START_CHARS_COUNT = sizeof(EPD_TEXT_NO_START_CHARS) / sizeof(EPD_TEXT_NO_START_CHARS[0]);

// 行末禁止文字（開き括弧など）
const uint32_t EPD_TEXT_NO_END_CHARS[] = {
    0xFF08, // （
    0x3008, // 〈
    0x300A, // 《
    0x300C, // 「
    0x300E, // 『
    0x3010, // 【
    0xFF08, // （
    0xFF5B, // ｛
    0xFF5F, // ｟
    0x3014, // 〔
    0x2018, // '
    0x201C, // "
    0x3016, // 〖
    0x3018, // 〘
    0x301A, // 〚
    0xFF3B, // ［
    0xFF5B  // ｛
};
const int EPD_TEXT_NO_END_CHARS_COUNT = sizeof(EPD_TEXT_NO_END_CHARS) / sizeof(EPD_TEXT_NO_END_CHARS[0]);

/**
 * @brief テキスト描画設定を初期化する
 * @param config 初期化する設定構造体へのポインタ
 * @param font 使用するフォント情報
 */
void epd_text_config_init(EPDTextConfig *config, const FontInfo *font)
{
    if (config == NULL)
    {
        ESP_LOGE(TAG, "Invalid config pointer");
        return;
    }

    // 構造体をゼロクリア
    memset(config, 0, sizeof(EPDTextConfig));

    // フォント関連の設定
    config->font = font;
    config->text_color = 0x00; // デフォルトで黒色（0）
    config->bold = false;      // デフォルトで太字なし

    // レイアウト関連の設定
    config->vertical = false;                // デフォルトで横書き
    config->line_spacing = 4;                // デフォルト行間
    config->char_spacing = 0;                // デフォルト文字間隔
    config->alignment = EPD_TEXT_ALIGN_LEFT; // デフォルトで左揃え
    config->rotate_non_cjk = true;           // 縦書き時に非CJK文字を回転

    // 装飾関連の設定
    config->bg_color = 0x0F;       // デフォルトで白色（15）
    config->bg_transparent = true; // デフォルトで背景透明
    config->underline = false;     // デフォルトで下線なし

    // ルビ関連の設定
    config->enable_ruby = false; // デフォルトでルビ無効
    config->ruby_font = NULL;    // ルビ用フォントは未設定
    config->ruby_offset = 2;     // デフォルトルビオフセット

    // その他の設定
    config->wrap_width = 0;         // デフォルトで折り返しなし
    config->enable_kerning = false; // デフォルトでカーニング無効

    ESP_LOGI(TAG, "Text config initialized with font size %d", font ? font->size : 0);
}

/**
 * @brief フォントからコードポイントに対応する文字情報を検索する
 * @param font フォント情報
 * @param code_point 検索するUnicodeコードポイント
 * @return 該当する文字情報のポインタ。見つからない場合はNULL
 */
const FontCharInfo *epd_text_find_char(const FontInfo *font, uint32_t code_point)
{
    if (font == NULL || font->chars == NULL || font->chars_count == 0)
    {
        return NULL;
    }

    // バイナリサーチでコードポイントを検索
    int left = 0;
    int right = font->chars_count - 1;

    while (left <= right)
    {
        int mid = left + (right - left) / 2;
        if (font->chars[mid].code_point == code_point)
        {
            return &font->chars[mid];
        }
        else if (font->chars[mid].code_point < code_point)
        {
            left = mid + 1;
        }
        else
        {
            right = mid - 1;
        }
    }

    // 見つからなかった場合
    ESP_LOGD(TAG, "Character U+%08lX not found in font", code_point);
    return NULL;
}

/**
 * @brief UTF-8テキストから次の文字のUnicodeコードポイントを取得する
 * @param text UTF-8テキストへのポインタのポインタ（処理後、ポインタは次の文字位置に進む）
 * @return 次の文字のUnicodeコードポイント。文字列終端の場合は0
 */
uint32_t epd_text_utf8_next_char(const char **text)
{
    if (text == NULL || *text == NULL || **text == '\0')
    {
        return 0;
    }

    const unsigned char *s = (const unsigned char *)(*text);
    uint32_t code_point = 0;
    int bytes_to_read = 0;

    // UTF-8エンコーディングを解析
    if (*s < 0x80)
    {
        // 1バイト文字 (ASCII)
        code_point = *s;
        bytes_to_read = 1;
    }
    else if ((*s & 0xE0) == 0xC0)
    {
        // 2バイト文字
        code_point = (*s & 0x1F);
        bytes_to_read = 2;
    }
    else if ((*s & 0xF0) == 0xE0)
    {
        // 3バイト文字
        code_point = (*s & 0x0F);
        bytes_to_read = 3;
    }
    else if ((*s & 0xF8) == 0xF0)
    {
        // 4バイト文字
        code_point = (*s & 0x07);
        bytes_to_read = 4;
    }
    else
    {
        // 無効なUTF-8シーケンス - スキップして次へ
        (*text)++;
        ESP_LOGW(TAG, "Invalid UTF-8 sequence");
        return 0xFFFD; // Replacement character
    }

    // 残りのバイトを読み込む
    s++;
    for (int i = 1; i < bytes_to_read; i++)
    {
        if ((*s & 0xC0) != 0x80)
        {
            // 無効な継続バイト
            *text += i;
            ESP_LOGW(TAG, "Invalid UTF-8 continuation byte");
            return 0xFFFD; // Replacement character
        }
        code_point = (code_point << 6) | (*s & 0x3F);
        s++;
    }

    // ポインタを進める
    *text += bytes_to_read;
    return code_point;
}

/**
 * @brief 文字が日本語/中国語/韓国語かどうかを判定する
 * @param code_point Unicodeコードポイント
 * @return CJK文字ならtrue、それ以外ならfalse
 */
bool epd_text_is_cjk(uint32_t code_point)
{
    // 基本的なCJK範囲の確認
    // 日本語、中国語、韓国語の主要な文字範囲をチェック

    // CJKユニファイド漢字
    if ((code_point >= 0x4E00 && code_point <= 0x9FFF) ||   // CJK統合漢字
        (code_point >= 0x3400 && code_point <= 0x4DBF) ||   // CJK統合漢字拡張A
        (code_point >= 0x20000 && code_point <= 0x2A6DF) || // CJK統合漢字拡張B
        (code_point >= 0x2A700 && code_point <= 0x2B73F) || // CJK統合漢字拡張C
        (code_point >= 0x2B740 && code_point <= 0x2B81F) || // CJK統合漢字拡張D
        (code_point >= 0x2B820 && code_point <= 0x2CEAF) || // CJK統合漢字拡張E
        (code_point >= 0x2CEB0 && code_point <= 0x2EBEF) || // CJK統合漢字拡張F
        (code_point >= 0x30000 && code_point <= 0x3134F) || // CJK統合漢字拡張G
        (code_point >= 0xF900 && code_point <= 0xFAFF) ||   // CJK互換漢字
        (code_point >= 0x2F800 && code_point <= 0x2FA1F))
    { // CJK互換漢字補助
        return true;
    }

    // 日本語特有
    if ((code_point >= 0x3040 && code_point <= 0x309F) || // ひらがな
        (code_point >= 0x30A0 && code_point <= 0x30FF) || // カタカナ
        (code_point >= 0x31F0 && code_point <= 0x31FF))
    { // カタカナ拡張
        return true;
    }

    // 韓国語（ハングル）
    if ((code_point >= 0xAC00 && code_point <= 0xD7AF) || // ハングル音節
        (code_point >= 0x1100 && code_point <= 0x11FF) || // ハングル字母
        (code_point >= 0x3130 && code_point <= 0x318F))
    { // ハングル互換字母
        return true;
    }

    // その他のCJK関連文字
    if ((code_point >= 0x3000 && code_point <= 0x303F) || // CJK記号と句読点
        (code_point >= 0xFF00 && code_point <= 0xFFEF))
    { // 全角形と半角形
        return true;
    }

    return false;
}

/**
 * @brief 単一の文字を描画する
 * @param wrapper EPDラッパー構造体へのポインタ
 * @param x X座標
 * @param y Y座標
 * @param code_point 描画する文字のUnicodeコードポイント
 * @param config テキスト描画設定
 * @return 描画した文字の幅
 */
int epd_text_draw_char(EPDWrapper *wrapper, int x, int y, uint32_t code_point, const EPDTextConfig *config)
{
    if (wrapper == NULL || config == NULL || config->font == NULL)
    {
        ESP_LOGE(TAG, "Invalid parameters for drawing character");
        return 0;
    }

    // フォントデータから文字情報を取得
    const FontCharInfo *char_info = epd_text_find_char(config->font, code_point);
    if (char_info == NULL)
    {
        ESP_LOGW(TAG, "Character U+%04lX not found in font", code_point);
        return 0;
    }

    // 文字色の決定（0x00または0xFFのみ対応）
    uint8_t draw_color;
    if (config->text_color == 0xFF)
    {
        draw_color = 0xFF; // 白
    }
    else
    {
        draw_color = 0x00; // 黒
    }

    // ビットマップデータの取得
    const uint8_t *bitmap = config->font->bitmap_data + char_info->data_offset;

    ESP_LOGI(TAG, "Drawing character U+%04lX at (%d,%d)", code_point, x, y);
    ESP_LOGI(TAG, "Character info - width: %d, img_width: %d, img_height: %d",
             char_info->width, char_info->img_width, char_info->img_height);

    // 文字の描画
    for (int dy = 0; dy < char_info->img_height+4; dy++)
    {
        for (int dx = 0; dx < char_info->img_width; dx++)
        {
            // ビットマップデータの位置を計算
            int byte_index = (dy * ((char_info->img_width + 7) / 8)) + (dx / 8);
            int bit_index = 7 - (dx % 8); // MSBファースト（左から右へ）

            // ビットが立っていれば描画（1=描画、0=透明）
            bool pixel_is_set = (bitmap[byte_index] & (1 << bit_index)) != 0;
            if (pixel_is_set)
            {
                epd_draw_pixel(x + dx, y + dy, draw_color, wrapper->framebuffer);
                //ESP_LOGI(TAG, "*");
            }else{
                //ESP_LOGI(TAG, "_");
            }
        }
        
        //ESP_LOGI(TAG, "##############");
    }

    if (config->vertical)
    {
        // 縦書きの場合は高さを返す
        return char_info->img_height;
    }
    else
    {
        // 描画した文字の幅を返す
        return char_info->width;
    }
}

/**
 * @brief UTF-8文字列を描画する
 * @param wrapper EPDラッパー構造体へのポインタ
 * @param x X座標
 * @param y Y座標
 * @param text 描画するUTF-8文字列
 * @param config テキスト描画設定
 * @return 描画した文字列の幅（横書き時）または高さ（縦書き時）
 */
int epd_text_draw_string(EPDWrapper *wrapper, int x, int y, const char *text, const EPDTextConfig *config)
{
    if (wrapper == NULL || config == NULL || config->font == NULL || text == NULL)
    {
        ESP_LOGE(TAG, "Invalid parameters for drawing string");
        return 0;
    }

    // 表示領域の取得（画面サイズ）
    int screen_width = epd_wrapper_get_width(wrapper);
    int screen_height = epd_wrapper_get_height(wrapper);

    ESP_LOGI(TAG, "Drawing string at (%d,%d): %s", x, y, text);
    ESP_LOGI(TAG, "Mode: %s", config->vertical ? "Vertical" : "Horizontal");

    // 文字描画の初期位置
    int current_x = x;
    int current_y = y;

    // 文字列の描画後のトータル幅/高さを追跡
    int total_advance = 0;

    // UTF-8テキストの解析
    const char *ptr = text;
    uint32_t code_point;

    while ((code_point = epd_text_utf8_next_char(&ptr)) != 0)
    {
        // フォントから文字情報を取得
        const FontCharInfo *char_info = epd_text_find_char(config->font, code_point);
        if (char_info == NULL)
        {
            ESP_LOGW(TAG, "Character U+%04lX not found in font, skipping", code_point);
            continue;
        }

        // 画面外の描画を防止するためのチェック
        if (config->vertical)
        {
            // 縦書きの場合
            if (current_y + char_info->img_height > screen_height)
            {
                ESP_LOGW(TAG, "String extends beyond bottom edge, stopping");
                break;
            }
        }
        else
        {
            // 横書きの場合
            if (current_x + char_info->img_width > screen_width)
            {
                ESP_LOGW(TAG, "String extends beyond right edge, stopping");
                break;
            }
        }

        // 文字を描画
        int advance = epd_text_draw_char(wrapper, current_x, current_y, code_point, config);

        // 下線の描画（横書きの場合のみ）
        if (config->underline && !config->vertical)
        {
            // 下線の位置（文字の下部から2ピクセル下）
            int underline_y = current_y + config->font->max_height + 2;
            // 文字の幅だけ横線を引く
            epd_wrapper_draw_line(wrapper,
                                  current_x,
                                  underline_y,
                                  current_x + char_info->width,
                                  underline_y,
                                  config->text_color == 0xFF ? 0xFF : 0x00);
        }

        // 次の文字位置の計算（文字間隔を考慮）
        if (config->vertical)
        {
            // 縦書きの場合は下に進む
            current_y += advance + config->char_spacing;
            total_advance += advance + config->char_spacing;
        }
        else
        {
            // 横書きの場合は右に進む
            current_x += advance + config->char_spacing;
            total_advance += advance + config->char_spacing;
        }
    }

    // 最後の文字間隔を差し引く（追加されたが実際には使われない）
    if (total_advance > 0)
    {
        total_advance -= config->char_spacing;
    }

    // 描画した文字列全体の幅（横書き）または高さ（縦書き）を返す
    return total_advance;
}

/**
 * @brief 文字が行頭禁止文字かどうかを判定する
 * @param code_point 判定するUnicodeコードポイント
 * @return 行頭禁止文字の場合はtrue、それ以外はfalse
 */
bool epd_text_is_no_start_char(uint32_t code_point)
{
    for (int i = 0; i < EPD_TEXT_NO_START_CHARS_COUNT; i++)
    {
        if (code_point == EPD_TEXT_NO_START_CHARS[i])
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief 文字が行末禁止文字かどうかを判定する
 * @param code_point 判定するUnicodeコードポイント
 * @return 行末禁止文字の場合はtrue、それ以外はfalse
 */
bool epd_text_is_no_end_char(uint32_t code_point)
{
    for (int i = 0; i < EPD_TEXT_NO_END_CHARS_COUNT; i++)
    {
        if (code_point == EPD_TEXT_NO_END_CHARS[i])
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief テキストの描画幅を計算する
 * @param text 計算するUTF-8文字列
 * @param config テキスト描画設定
 * @return テキストの描画幅（ピクセル単位）
 */
int epd_text_calc_width(const char *text, const EPDTextConfig *config)
{
    if (text == NULL || config == NULL || config->font == NULL)
    {
        ESP_LOGE(TAG, "Invalid parameters for calculating text width");
        return 0;
    }

    // 文字列の解析
    const char *ptr = text;
    uint32_t code_point;
    int total_width = 0;

    while ((code_point = epd_text_utf8_next_char(&ptr)) != 0)
    {
        // フォントから文字情報を取得
        const FontCharInfo *char_info = epd_text_find_char(config->font, code_point);
        if (char_info == NULL)
        {
            continue; // 文字が見つからない場合はスキップ
        }

        // 幅を加算（文字間隔も考慮）
        total_width += char_info->width + config->char_spacing;
    }

    // 最後の文字間隔を差し引く
    if (total_width > 0)
    {
        total_width -= config->char_spacing;
    }

    return total_width;
}

/**
 * @brief テキストの描画高さを計算する
 * @param text 計算するUTF-8文字列
 * @param config テキスト描画設定
 * @return テキストの描画高さ（ピクセル単位）
 */
int epd_text_calc_height(const char *text, const EPDTextConfig *config)
{
    if (text == NULL || config == NULL || config->font == NULL)
    {
        ESP_LOGE(TAG, "Invalid parameters for calculating text height");
        return 0;
    }

    // フォントの高さを取得
    int line_height = config->font->max_height;

    // 縦書きモードの場合は個別に文字の高さを計算
    if (config->vertical)
    {
        // 文字列の解析
        const char *ptr = text;
        uint32_t code_point;
        int total_height = 0;

        while ((code_point = epd_text_utf8_next_char(&ptr)) != 0)
        {
            // フォントから文字情報を取得
            const FontCharInfo *char_info = epd_text_find_char(config->font, code_point);
            if (char_info == NULL)
            {
                continue; // 文字が見つからない場合はスキップ
            }

            // 高さを加算（文字間隔も考慮）
            total_height += char_info->img_height + config->char_spacing;
        }

        // 最後の文字間隔を差し引く
        if (total_height > 0)
        {
            total_height -= config->char_spacing;
        }

        return total_height;
    }
    else
    {
        // 横書きモード: 単純に文字の高さを返す
        return line_height;
    }
}

/**
 * @brief 複数行のテキストを描画する
 * @param wrapper EPDラッパー構造体へのポインタ
 * @param x X座標
 * @param y Y座標
 * @param rect 描画領域（この範囲内にテキストを収める）
 * @param text 描画するUTF-8文字列
 * @param config テキスト描画設定
 * @return 描画した行数
 */
int epd_text_draw_multiline(EPDWrapper *wrapper, int x, int y, EpdRect *rect, const char *text, const EPDTextConfig *config)
{
    if (wrapper == NULL || config == NULL || config->font == NULL || text == NULL || rect == NULL)
    {
        ESP_LOGE(TAG, "Invalid parameters for drawing multiline text");
        return 0;
    }

    // ローカルにconfig情報をコピーして変更可能にする
    EPDTextConfig local_config = *config;

    // 矩形領域の幅を折り返し幅として設定
    local_config.wrap_width = rect->width;

    ESP_LOGI(TAG, "Drawing multiline text in rect: %d,%d [%dx%d]",
             rect->x, rect->y, rect->width, rect->height);
    ESP_LOGI(TAG, "Mode: %s, Wrap width: %d",
             local_config.vertical ? "Vertical" : "Horizontal", local_config.wrap_width);

    // 現在の描画位置
    int current_x = x;
    int current_y = y;

    // 行の高さ（横書き）または幅（縦書き）
    int line_size = local_config.vertical ? local_config.font->max_height : local_config.font->max_height + local_config.line_spacing;

    // 描画可能な行数を計算
    int max_lines = local_config.vertical ? rect->width / line_size : rect->height / line_size;

    // バッファサイズを計算（UTF-8で4バイト文字を考慮）
    int buffer_size = strlen(text) + 1;
    char *text_copy = (char *)malloc(buffer_size);
    if (text_copy == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for text copy");
        return 0;
    }

    // テキストをコピー
    strcpy(text_copy, text);

    // 文字列を改行でトークン化
    char *token = strtok(text_copy, "\n");
    int line_count = 0;

    while (token != NULL && line_count < max_lines)
    {
        // この行の文字列を処理
        int token_length = strlen(token);

        // 描画すべき部分文字列のリスト
        const char *segments[100]; // 十分な大きさの配列
        int segment_count = 0;

        // 縦書きモードの場合、単純に文字列を描画
        if (local_config.vertical)
        {
            segments[0] = token;
            segment_count = 1;
        }
        else
        {
            // 横書きモードの場合、折り返し処理が必要
            const char *line_start = token;
            const char *current = token;
            const char *segment_start = token;
            int segment_width = 0;
            uint32_t last_code_point = 0;
            bool pending_wrap = false;
            
            // 前の文字を保存するためのバッファ
            const char* prev_char_start = NULL;

            while (*current != '\0')
            {
                const char *char_start = current;
                uint32_t code_point = epd_text_utf8_next_char(&current);

                // 文字情報を取得
                const FontCharInfo *char_info = epd_text_find_char(local_config.font, code_point);
                if (char_info == NULL)
                {
                    continue; // 文字が見つからない場合はスキップ
                }

                // この文字を追加した場合の幅を計算
                int new_width = segment_width + char_info->width;
                if (segment_width > 0)
                {
                    new_width += local_config.char_spacing;
                }

                // 幅が制限を超える場合、または前の処理で折り返しが保留になっていた場合の処理
                if (new_width > local_config.wrap_width || pending_wrap)
                {
                    pending_wrap = false;
                    
                    // 行頭禁止文字の場合
                    if (epd_text_is_no_start_char(code_point))
                    {
                        // 前の文字で折り返す
                        if (prev_char_start != NULL && segment_start < prev_char_start)
                        {
                            // 現在のセグメントを前の文字までとして追加
                            segments[segment_count++] = segment_start;
                            
                            // 前の文字から始まる新しいセグメントを開始
                            segment_start = prev_char_start;
                            
                            // 前の文字と現在の文字の幅を計算
                            const FontCharInfo *prev_char_info = epd_text_find_char(local_config.font, last_code_point);
                            segment_width = (prev_char_info != NULL ? prev_char_info->width : 0) + 
                                           local_config.char_spacing + char_info->width;
                        }
                        else
                        {
                            // 前の文字がない場合は通常の折り返し
                            segments[segment_count++] = segment_start;
                            segment_start = char_start;
                            segment_width = char_info->width;
                        }
                    }
                    // 行末禁止文字だった場合
                    else if (epd_text_is_no_end_char(last_code_point))
                    {
                        // 行末禁止文字と現在の文字を次の行に送る
                        if (prev_char_start != NULL && segment_start < prev_char_start)
                        {
                            // 直前の行末禁止文字の前までをセグメントとして追加
                            segments[segment_count++] = segment_start;
                            
                            // 行末禁止文字から新しいセグメントを開始
                            segment_start = prev_char_start;
                            
                            // 行末禁止文字と現在の文字の幅を計算
                            const FontCharInfo *prev_char_info = epd_text_find_char(local_config.font, last_code_point);
                            segment_width = (prev_char_info != NULL ? prev_char_info->width : 0) + 
                                           local_config.char_spacing + char_info->width;
                        }
                        else
                        {
                            // 通常の折り返し（最初の文字が行末禁止文字の場合）
                            segments[segment_count++] = segment_start;
                            segment_start = char_start;
                            segment_width = char_info->width;
                        }
                    }
                    else
                    {
                        // 通常の折り返し
                        segments[segment_count++] = segment_start;
                        segment_start = char_start;
                        segment_width = char_info->width;
                    }
                }
                else
                {
                    // 次の文字を見て、それが行頭禁止文字かつ現在の行に収まらない場合、
                    // 現在の文字で折り返しを保留
                    const char *next_char_start = current;
                    if (*next_char_start != '\0')
                    {
                        const char *next_current = next_char_start;
                        uint32_t next_code_point = epd_text_utf8_next_char(&next_current);
                        
                        if (epd_text_is_no_start_char(next_code_point))
                        {
                            // 次の文字の情報を取得
                            const FontCharInfo *next_char_info = epd_text_find_char(local_config.font, next_code_point);
                            if (next_char_info != NULL)
                            {
                                // 次の文字を追加した場合の幅を計算
                                int next_width = new_width + next_char_info->width + local_config.char_spacing;
                                
                                if (next_width > local_config.wrap_width)
                                {
                                    // 行頭禁止文字が次の行に来てしまう場合、現在の文字でも折り返す
                                    pending_wrap = true;
                                }
                            }
                        }
                    }
                    
                    // 幅が制限内の場合、幅を更新
                    segment_width = new_width;
                }

                // 前の文字の位置を記録
                prev_char_start = char_start;
                last_code_point = code_point;
            }

            // 最後のセグメントを追加
            if (*segment_start != '\0')
            {
                segments[segment_count++] = segment_start;
            }
        }

        // 各セグメントを描画
        for (int i = 0; i < segment_count && line_count < max_lines; i++)
        {
            // このセグメントの長さを取得（次のセグメントまでか文字列終端まで）
            int segment_len = (i < segment_count - 1) ? (segments[i + 1] - segments[i]) : strlen(segments[i]);

            // このセグメントをnull終端の文字列にコピー
            char *segment_str = (char *)malloc(segment_len + 1);
            if (segment_str == NULL)
            {
                ESP_LOGE(TAG, "Failed to allocate memory for segment");
                continue;
            }

            memcpy(segment_str, segments[i], segment_len);
            segment_str[segment_len] = '\0';

            // 描画領域を超えていないか確認
            if (local_config.vertical)
            {
                if (current_x + line_size > rect->x + rect->width)
                {
                    free(segment_str);
                    break;
                }
            }
            else
            {
                if (current_y + line_size > rect->y + rect->height)
                {
                    free(segment_str);
                    break;
                }
            }

            // セグメントを描画
            if (local_config.vertical)
            {
                epd_text_draw_string(wrapper, current_x, current_y, segment_str, &local_config);
                current_x += line_size; // 次の行（縦書きでは右に移動）
            }
            else
            {
                epd_text_draw_string(wrapper, current_x, current_y, segment_str, &local_config);
                current_y += line_size; // 次の行（横書きでは下に移動）
            }

            free(segment_str);
            line_count++;
        }

        // 次の行を取得
        token = strtok(NULL, "\n");
    }

    free(text_copy);
    return line_count;
}

int epd_text_draw_ruby(EPDWrapper *wrapper, int x, int y, const char *text, const char *ruby, const EPDTextConfig *config)
{
    ESP_LOGW(TAG, "epd_text_draw_ruby not fully implemented yet");
    return 0;
}
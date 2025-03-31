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

// 全角・半角スペース用文字データ
FontCharInfo harf_sp = {0x0020, 0U, 7, 16, 0U, 0, 0, 0};
FontCharInfo full_sp = {0x3000, 0U, 7, 16, 0U, 0, 0, 0};

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
    config->box_padding = 0;        // デフォルトでパディング0
    config->wrap_width = 0;         // デフォルトで折り返しなし
    config->enable_kerning = false; // デフォルトでカーニング無効
    config->mono_spacing = false;   // デフォルトでプロポーショナル

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
    // 半角スペース
    if (code_point == 0x0020)
    {
        harf_sp.img_width = font->size / 2;
        harf_sp.img_height = font->size;
        harf_sp.img_width = font->size / 2;
        return &harf_sp;
    }
    // 全角スペース
    if (code_point == 0x3000)
    {
        full_sp.img_width = font->size;
        full_sp.img_height = font->size;
        full_sp.img_width = font->size;
        return &full_sp;
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
 * @brief 回転を考慮して文字を描画する
 * @param wrapper EPDラッパー構造体へのポインタ
 * @param x 描画開始X座標
 * @param y 描画開始Y座標
 * @param char_info 描画する文字情報
 * @param bitmap ビットマップデータ
 * @param rotation 回転角度（0:0度, 1:90度, 2:180度, 3:270度）
 * @param text_color 文字色
 * @param bg_color 背景色
 * @param bg_transparent 背景透過フラグ
 */
void draw_rotated_char(EPDWrapper *wrapper, int x, int y,
                       const FontCharInfo *char_info, const uint8_t *bitmap,
                       int rotation, uint8_t text_color, uint8_t bg_color,
                       bool bg_transparent)
{
    // スペースの場合はスキップ
    if (char_info->code_point == 0x0020 || char_info->code_point == 0x3000)
        return;

    int width = char_info->img_width;
    int height = char_info->img_height;

    // ビットマップデータをバイト単位でアクセスするための計算
    int bytes_per_row = (width + 7) / 8; // 8ビット境界に切り上げ

    // 各ピクセルを描画
    for (int dy = 0; dy < height; dy++)
    {
        for (int dx = 0; dx < width; dx++)
        {
            // ビットマップデータの位置を計算
            int byte_index = (dy * bytes_per_row) + (dx / 8);
            int bit_index = 7 - (dx % 8); // MSBファースト

            // ビットが立っているかチェック
            bool pixel_is_set = (bitmap[byte_index] & (1 << bit_index)) != 0;

            // 描画先の座標を回転に応じて計算
            int draw_x, draw_y;

            switch (rotation)
            {
            case 0: // 0度回転（そのまま）
                draw_x = x + dx;
                draw_y = y + dy;
                break;

            case 1: // 90度回転（時計回り）
                draw_x = x + height - 1 - dy;
                draw_y = y + dx;
                break;

            case 2: // 180度回転
                draw_x = x + width - 1 - dx;
                draw_y = y + height - 1 - dy;
                break;

            case 3: // 270度回転（反時計回り）
                draw_x = x + dy;
                draw_y = y + width - 1 - dx;
                break;

            default:
                // デフォルトは回転なし
                draw_x = x + dx;
                draw_y = y + dy;
            }

            // ピクセルを描画（文字の場合）またはクリア（背景の場合）
            if (pixel_is_set)
            {
                epd_wrapper_draw_pixel(wrapper, draw_x, draw_y, text_color);
            }
            else if (!bg_transparent)
            {
                // 背景が透明でない場合のみ背景色を描画
                epd_wrapper_draw_pixel(wrapper, draw_x, draw_y, bg_color);
            }
        }
    }
}

/**
 * @brief 単一の文字を描画する（回転対応版）
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
        return 0;
    }

    // フォントデータから文字情報を取得
    const FontCharInfo *char_info = epd_text_find_char(config->font, code_point);
    if (char_info == NULL)
    {
        return 0;
    }

    // 文字色の決定
    uint8_t draw_color = config->text_color;
    uint8_t bg_color = config->bg_color;

    /*
    // ベースライン調整
    int y_pos = y;
    if (config->use_baseline)
    {
        y_pos = y - config->font->baseline;
    }

    // 位置オフセットの適用（タイポグラフィ情報が有効な場合）
    if (config->use_typo_flags)
    {
        x += char_info->x_offset;
        y_pos += char_info->y_offset;
    }
    */

    // ビットマップデータの取得
    const uint8_t *bitmap = config->font->bitmap_data + char_info->data_offset;

    // 回転角度の決定
    int rotation = 0;
    if (config->vertical)
    {
        rotation = char_info->rotation;
    }

    // 回転方向によってオフセットを追加
    int x_pos = x;
    int y_pos = y;
    switch (rotation)
    {
    case 0: // 0度回転（そのまま）
        y_pos += char_info->y_offset;
        // 等幅または縦書きの場合、左右中央揃えにする
        if (config->mono_spacing || config->vertical)
        {
            x_pos += (config->font->max_width - char_info->img_width) / 2;
        }
        break;

    case 3: // 270度回転
    case 1: // 90度回転（時計回り）縦書きの一部記号「」ー
        int sub_offset = config->font->max_width - char_info->img_height - char_info->y_offset;
        x_pos += sub_offset > 0 ? sub_offset : 0;
        y_pos += char_info->x_offset;
        break;

    case 2: // 180度回転 縦書きの一部記号、。
        x_pos += config->font->max_width - char_info->img_height;
        break;

    default:
        // デフォルトは回転なし
        y_pos += char_info->y_offset;
        break;
    }

    // 文字の描画（回転を考慮）
    draw_rotated_char(
        wrapper,
        x_pos,
        y_pos,
        char_info,
        bitmap,
        rotation,
        draw_color,
        bg_color,
        config->bg_transparent);

    // 下線の描画（横書きの場合のみ）
    if (config->underline && !config->vertical)
    {
        // 回転角度によって下線の位置を調整
        if (rotation == 0 || rotation == 2)
        {
            // 0度または180度の場合は通常の下線位置
            int underline_y = y + config->font->max_height + 2;
            // 文字の幅だけ横線を引く
            epd_wrapper_draw_line(wrapper, x, underline_y, x + char_info->img_width, underline_y, draw_color);
        }
        else
        {
            // 90度または270度回転時の下線（横に表示される文字に対して）
            int underline_x = (rotation == 1) ? x - 2 : x + char_info->img_height + 2;
            epd_wrapper_draw_line(wrapper, underline_x, y, underline_x, y + char_info->img_width, draw_color);
        }
    }

    // 等幅とプロポーショナルで進んだサイズが違う。９０度回転した時も
    if (rotation == 1)
    {
        return config->mono_spacing ? config->font->max_height : char_info->img_height;
    }
    if (config->mono_spacing)
    {
        return config->vertical ? config->font->max_height : config->font->max_width;
    }
    else
    {
        return config->vertical ? char_info->img_height : char_info->img_width;
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
bool epd_text_is_no_start_char(const FontCharInfo *font_char)
{
    return (font_char->typo_flags & TYPO_FLAG_NO_BREAK_START) != 0;
}

/**
 * @brief 文字が行末禁止文字かどうかを判定する
 * @param code_point 判定するUnicodeコードポイント
 * @return 行末禁止文字の場合はtrue、それ以外はfalse
 */
bool epd_text_is_no_end_char(const FontCharInfo *font_char)
{
    return (font_char->typo_flags & TYPO_FLAG_NO_BREAK_END) != 0;
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
int epd_text_draw_multiline(EPDWrapper *wrapper, EpdRect *rect, const char *text, const EPDTextConfig *config)
{
    if (wrapper == NULL || config == NULL || config->font == NULL || text == NULL || rect == NULL)
    {
        ESP_LOGE(TAG, "Invalid parameters for drawing multiline text");
        return 0;
    }

    // ローカルにconfig情報をコピーして変更可能にする
    EPDTextConfig local_config = *config;

    // 矩形領域の幅を折り返し幅として設定
    local_config.wrap_width = local_config.vertical ? rect->height - (config->box_padding * 2) : rect->width - (config->box_padding * 2);

    ESP_LOGI(TAG, "Drawing multiline text in rect: %d,%d [%dx%d]",
             rect->x, rect->y, rect->width, rect->height);
    ESP_LOGI(TAG, "Mode: %s, Wrap width: %d",
             local_config.vertical ? "Vertical" : "Horizontal", local_config.wrap_width);

    // 現在の描画位置
    int current_x = rect->x + config->box_padding;
    int current_y = rect->y + config->box_padding;

    // 縦書きの場合は描画開始位置は右上-一行幅
    if (local_config.vertical)
        current_x = rect->x + rect->width - config->box_padding - local_config.font->max_width;

    // 行の高さ（横書き）または幅（縦書き）
    int line_size = local_config.vertical ? local_config.font->max_width + local_config.line_spacing : local_config.font->max_height + local_config.line_spacing;

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
        // int token_length = strlen(token);

        // 描画すべき部分文字列のリスト
        const char *segments[100]; // 十分な大きさの配列
        int segment_count = 0;

        // 縦書きモードの場合、単純に文字列を描画
        /*if (local_config.vertical)
        {
            segments[0] = token;
            segment_count = 1;
        }
        else
        {*/
        // 折り返し処理用
        // const char *line_start = token;
        const char *current = token;
        const char *segment_start = token;
        int segment_width = 0;
        int segment_height = 0;
        uint32_t last_code_point = 0;
        bool pending_wrap = false;

        // 前の文字を保存するためのバッファ
        const char *prev_char_start = NULL;

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
            // mono spacingかproportionalかで幅と高さを変える
            int char_width;
            int char_height;
            if (config->mono_spacing)
            {
                // 等幅はフォント情報から最大幅と最大高さを取得
                char_width = local_config.font->max_width;
                char_height = local_config.font->max_height;
            }
            else
            {
                // プロポーショナルは実際の文字の幅と高さを取得
                char_width = char_info->img_width;
                char_height = char_info->img_height;
            }
            // 前回の文字情報
            const FontCharInfo *prev_char_info;
            if (last_code_point != 0)
            {
                prev_char_info = epd_text_find_char(local_config.font, last_code_point);
            }
            // 前回文字の幅と高さ
            int prev_char_width;
            int prev_char_height;
            if (config->mono_spacing)
            {
                // 等幅はフォント情報から最大幅と最大高さを取得
                prev_char_width = local_config.font->max_width;
                prev_char_height = local_config.font->max_height;
            }
            else
            {
                // プロポーショナルは実際の文字の幅と高さを取得
                prev_char_width = prev_char_info->img_width;
                prev_char_height = prev_char_info->img_height;
            }
            // この文字を追加した場合の幅を計算（横書き用）
            int new_width = segment_width + char_width;
            if (segment_width > 0)
            {
                new_width += local_config.char_spacing;
            }

            // この文字を追加した場合の高さを計算（縦書き用）
            int new_height = segment_height + char_height;
            if (segment_height > 0)
            {
                new_height += local_config.char_spacing;
            }

            // 幅、高さが制限を超えているかチェック
            bool size_limit; // = (local_config.vertical ? new_height : new_width ) > local_config.wrap_width - local_config.box_padding;

            if (local_config.vertical)
            {
                size_limit = new_height > local_config.wrap_width - (local_config.box_padding * 2);
            }
            else
            {
                size_limit = new_width > local_config.wrap_width - (local_config.box_padding * 2);
            }

            // 幅が制限を超える場合、または前の処理で折り返しが保留になっていた場合の処理
            if (size_limit || pending_wrap)
            {
                ESP_LOGI(TAG, "---------------size limit oVer!! wrap_max = %d", local_config.wrap_width);
                pending_wrap = false;

                // 表示しようとしている文字が行頭禁止文字の場合
                if (epd_text_is_no_start_char(char_info))
                {
                    // 前の文字の表示前で折り返す
                    if (prev_char_start != NULL && segment_start < prev_char_start)
                    {
                        // 現在のセグメントを前の文字までとして追加
                        segments[segment_count++] = segment_start;

                        // 前の文字から始まる新しいセグメントを開始
                        segment_start = prev_char_start;

                        // 前の文字と現在の文字の幅を計算
                        segment_width = (prev_char_info != NULL ? prev_char_width : 0) +
                                        local_config.char_spacing + char_width;
                        segment_height = (prev_char_info != NULL ? prev_char_height : 0) +
                                         local_config.char_spacing + char_height;
                    }
                    else
                    {
                        // 前の文字がない場合は通常の折り返し
                        segments[segment_count++] = segment_start;
                        segment_start = char_start;
                        segment_width = char_width;
                        segment_height = char_height;
                    }
                }
                // 行末禁止文字だった場合
                else if (epd_text_is_no_end_char(prev_char_info))
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
                        segment_width = (prev_char_info != NULL ? prev_char_width : 0) +
                                        local_config.char_spacing + char_width;
                        segment_height = (prev_char_info != NULL ? prev_char_height : 0) +
                                         local_config.char_spacing + char_height;
                    }
                    else
                    {
                        // 通常の折り返し（最初の文字が行末禁止文字の場合）
                        segments[segment_count++] = segment_start;
                        segment_start = char_start;
                        segment_width = char_width;
                        segment_height = char_height;
                    }
                }
                else
                {
                    // 通常の折り返し
                    segments[segment_count++] = segment_start;
                    segment_start = char_start;
                    segment_width = char_width;
                    segment_height = char_height;
                }
            }
            else
            {
                /*
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
                            int next_width = new_width + next_char_info->img_width + local_config.char_spacing;
                            // 次の文字を追加した場合の高さも計算
                            int next_height = new_height + next_char_info->img_height + local_config.char_spacing;

                            if (next_width > local_config.wrap_width || next_height > local_config.wrap_width)
                            {
                                // 行頭禁止文字が次の行に来てしまう場合、現在の文字でも折り返す
                                pending_wrap = true;
                            }
                        }
                    }
                }
                */
                // 幅が制限内の場合、幅と高さを更新
                segment_width = new_width;
                segment_height = new_height;
            }

            // 前の文字の位置を記録
            prev_char_start = char_start;
            last_code_point = code_point;

            //                ESP_LOGI(TAG, "---------------size now width:%d height:%d",new_width,new_height);
        }

        // 最後のセグメントを追加
        if (*segment_start != '\0')
        {
            segments[segment_count++] = segment_start;
        }
        //}

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
                if (current_x - line_size > rect->x + rect->width + config->box_padding)
                {
                    free(segment_str);
                    break;
                }
            }
            else
            {
                if (current_y + line_size > rect->y + rect->height - config->box_padding)
                {
                    free(segment_str);
                    break;
                }
            }

            // セグメントを描画
            if (local_config.vertical)
            {
                epd_text_draw_string(wrapper, current_x, current_y, segment_str, &local_config);
                current_x -= line_size; // 次の行（縦書きでは左に移動）
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
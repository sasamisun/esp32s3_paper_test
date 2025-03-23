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

// 禁則文字（行頭禁止文字）
static const uint32_t LINE_HEAD_PROHIBIT_CHARS[] = {
    0x3001, // 、
    0x3002, // 。
    0xFF0C, // ，
    0xFF0E, // ．
    0xFF01, // ！
    0xFF1F, // ？
    0x3009, // 〉
    0x300B, // 》
    0x300D, // 」
    0x300F, // 』
    0x3011, // 】
    0x3015, // 〕
    0x3017, // 〗
    0x3019, // 〙
    0x301B, // 〛
    0xFF09, // ）
    0xFF3D, // ］
    0xFF5D, // ｝
    0x0000  // 終端
};

// 禁則文字（行末禁止文字）
static const uint32_t LINE_TAIL_PROHIBIT_CHARS[] = {
    0x3008, // 〈
    0x300A, // 《
    0x300C, // 「
    0x300E, // 『
    0x3010, // 【
    0x3014, // 〔
    0x3016, // 〖
    0x3018, // 〘
    0x301A, // 〚
    0xFF08, // （
    0xFF3B, // ［
    0xFF5B, // ｛
    0x0000  // 終端
};

/**
 * @brief テキスト描画設定を初期化する
 */
void epd_text_config_init(EPDTextConfig* config, const FontInfo* font) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Invalid text config pointer");
        return;
    }

    // フォント関連
    config->font = font;
    config->text_color = 0x00; // 黒
    config->bold = false;
    
    // レイアウト関連
    config->vertical = false;
    config->line_spacing = font->size / 2; // フォントサイズの半分を行間に
    config->char_spacing = 0;
    config->alignment = EPD_TEXT_ALIGN_LEFT;
    config->rotate_non_cjk = true;
    
    // 装飾関連
    config->bg_color = 0x0F; // 白
    config->bg_transparent = true;
    config->underline = false;
    
    // ルビ関連
    config->enable_ruby = false;
    config->ruby_font = NULL;
    config->ruby_offset = 2;
    
    // その他の設定
    config->wrap_width = 0; // 折り返しなし
    config->enable_kerning = false;
    
    ESP_LOGI(TAG, "Text config initialized with font size %d", font->size);
}

/**
 * @brief テキストの描画高さを計算する
 */
int epd_text_calc_height(const char* text, const EPDTextConfig* config) {
    if (text == NULL || config == NULL || config->font == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for calculating text height");
        return 0;
    }
    
    int height = config->font->max_height;  // 最低でも1行分の高さ
    int width = 0;
    int max_width = 0;
    int lines = 1;
    
    if (config->vertical) {
        // 縦書きモードの場合、行数ではなく列数をカウント
        int columns = 1;
        width = 0;
        
        // テキストを1文字ずつ処理
        const char* p = text;
        uint32_t code_point;
        
        while ((code_point = epd_text_utf8_next_char(&p)) != 0) {
            // 改行文字の処理
            if (code_point == '\n') {
                columns++;
                if (width > max_width) {
                    max_width = width;
                }
                width = 0;
                continue;
            }
            
            // 文字情報を取得
            const FontCharInfo* char_info = epd_text_find_char(config->font, code_point);
            if (char_info) {
                width += char_info->width + config->char_spacing;
            } else {
                width += config->font->size / 2 + config->char_spacing;
            }
            
            // 折り返し幅を超えたら新しい列へ
            if (config->wrap_width > 0 && width > config->wrap_width) {
                columns++;
                width = 0;
            }
        }
        
        // 縦書きの場合の高さは、列数 * フォントの高さ + 列間隔
        return columns * config->font->max_height + (columns - 1) * config->line_spacing;
    } else {
        // 横書きモードの場合
        width = 0;
        
        // テキストを1文字ずつ処理
        const char* p = text;
        uint32_t code_point;
        
        while ((code_point = epd_text_utf8_next_char(&p)) != 0) {
            // 改行文字の処理
            if (code_point == '\n') {
                lines++;
                if (width > max_width) {
                    max_width = width;
                }
                width = 0;
                continue;
            }
            
            // 文字情報を取得
            const FontCharInfo* char_info = epd_text_find_char(config->font, code_point);
            if (char_info) {
                width += char_info->width + config->char_spacing;
            } else {
                width += config->font->size / 2 + config->char_spacing;
            }
            
            // 折り返し幅を超えたら新しい行へ
            if (config->wrap_width > 0 && width > config->wrap_width) {
                lines++;
                width = 0;
            }
        }
        
        // 横書きの場合の高さは、行数 * フォントの高さ + 行間隔
        return lines * config->font->max_height + (lines - 1) * config->line_spacing;
    }
}

/**
 * @brief UTF-8テキストを1行で描画する
 */
int epd_text_draw_string(EPDWrapper* wrapper, int x, int y, const char* text, const EPDTextConfig* config) {
    if (wrapper == NULL || text == NULL || config == NULL || config->font == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for drawing string");
        return 0;
    }
    
    ESP_LOGD(TAG, "Drawing string: '%s' at (%d,%d)", text, x, y);
    
    int total_width = 0;  // 描画した全体の幅
    int total_height = config->font->max_height;  // 高さは基本的にフォントの高さ
    
    // 先にテキスト全体の幅を計算（配置のために必要）
    if (config->alignment != EPD_TEXT_ALIGN_LEFT) {
        total_width = epd_text_calc_width(text, config);
    }
    
    // 描画開始位置を配置に応じて調整
    int start_x = x;
    int start_y = y;
    
    if (!config->vertical) {
        // 横書きモードの場合
        if (config->alignment == EPD_TEXT_ALIGN_CENTER) {
            start_x = x - (total_width / 2);
        } else if (config->alignment == EPD_TEXT_ALIGN_RIGHT) {
            start_x = x - total_width;
        }
    } else {
        // 縦書きモードの場合
        if (config->alignment == EPD_TEXT_ALIGN_CENTER) {
            start_y = y - (total_width / 2);
        } else if (config->alignment == EPD_TEXT_ALIGN_RIGHT) {
            start_y = y - total_width;
        }
    }
    
    // 現在の描画位置
    int cur_x = start_x;
    int cur_y = start_y;
    
    // テキストを1文字ずつ処理
    const char* p = text;
    uint32_t code_point;
    int char_width;
    
    while ((code_point = epd_text_utf8_next_char(&p)) != 0) {
        // 改行文字の処理
        if (code_point == '\n') {
            if (!config->vertical) {
                // 横書きモードでは次の行へ
                cur_x = start_x;
                cur_y += config->font->max_height + config->line_spacing;
                total_height += config->font->max_height + config->line_spacing;
            } else {
                // 縦書きモードでは左に新しい列
                cur_x -= config->font->max_height + config->line_spacing;
                cur_y = start_y;
            }
            continue;
        }
        
        // タブ文字の処理
        if (code_point == '\t') {
            // タブを4スペース分として処理
            for (int i = 0; i < 4; i++) {
                if (!config->vertical) {
                    char_width = epd_text_draw_char(wrapper, cur_x, cur_y, ' ', config);
                    cur_x += char_width;
                } else {
                    char_width = epd_text_draw_char(wrapper, cur_x, cur_y, ' ', config);
                    cur_y += char_width;
                }
            }
            continue;
        }
        
        // 通常の文字を描画
        if (!config->vertical) {
            // 横書きモード
            char_width = epd_text_draw_char(wrapper, cur_x, cur_y, code_point, config);
            cur_x += char_width;
        } else {
            // 縦書きモード
            char_width = epd_text_draw_char(wrapper, cur_x, cur_y, code_point, config);
            cur_y += char_width;
        }
    }
    
    // 描画した幅を返す
    if (!config->vertical) {
        return cur_x - start_x;
    } else {
        return cur_y - start_y;
    }
}

/**
 * @brief フォントからコードポイントに対応する文字情報を検索する（バイナリサーチ）
 */
const FontCharInfo* epd_text_find_char(const FontInfo* font, uint32_t code_point) {
    if (font == NULL || font->chars == NULL || font->chars_count == 0) {
        return NULL;
    }
    
    // バイナリサーチを実装
    int left = 0;
    int right = font->chars_count - 1;
    
    while (left <= right) {
        int mid = left + (right - left) / 2;
        
        if (font->chars[mid].code_point == code_point) {
            return &font->chars[mid];
        } else if (font->chars[mid].code_point < code_point) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    
    return NULL; // 見つからなかった
}

/**
 * @brief UTF-8テキストから次の文字のUnicodeコードポイントを取得する
 */
uint32_t epd_text_utf8_next_char(const char** text) {
    if (text == NULL || *text == NULL || **text == '\0') {
        return 0;
    }
    
    const unsigned char* s = (const unsigned char*)(*text);
    uint32_t code_point = 0;
    
    // UTF-8デコード
    if (s[0] < 0x80) { // 1バイト文字 (0xxxxxxx)
        code_point = s[0];
        *text += 1;
    } else if ((s[0] & 0xE0) == 0xC0) { // 2バイト文字 (110xxxxx 10xxxxxx)
        if (s[1] == '\0') {
            ESP_LOGW(TAG, "Incomplete UTF-8 sequence");
            *text += 1;
            return 0xFFFD; // 無効な文字として処理
        }
        code_point = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *text += 2;
    } else if ((s[0] & 0xF0) == 0xE0) { // 3バイト文字 (1110xxxx 10xxxxxx 10xxxxxx)
        if (s[1] == '\0' || s[2] == '\0') {
            ESP_LOGW(TAG, "Incomplete UTF-8 sequence");
            *text += 1;
            return 0xFFFD;
        }
        code_point = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *text += 3;
    } else if ((s[0] & 0xF8) == 0xF0) { // 4バイト文字 (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
        if (s[1] == '\0' || s[2] == '\0' || s[3] == '\0') {
            ESP_LOGW(TAG, "Incomplete UTF-8 sequence");
            *text += 1;
            return 0xFFFD;
        }
        code_point = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *text += 4;
    } else {
        ESP_LOGW(TAG, "Invalid UTF-8 sequence");
        *text += 1;
        return 0xFFFD;
    }
    
    return code_point;
}

/**
 * @brief 文字が日本語/中国語/韓国語かどうかを判定する
 */
bool epd_text_is_cjk(uint32_t code_point) {
    // CJK統合漢字
    if ((code_point >= 0x4E00 && code_point <= 0x9FFF) ||  // CJK統合漢字
        (code_point >= 0x3400 && code_point <= 0x4DBF) ||  // CJK統合漢字拡張A
        (code_point >= 0x20000 && code_point <= 0x2A6DF) || // CJK統合漢字拡張B
        (code_point >= 0x2A700 && code_point <= 0x2B73F) || // CJK統合漢字拡張C
        (code_point >= 0x2B740 && code_point <= 0x2B81F) || // CJK統合漢字拡張D
        (code_point >= 0x2B820 && code_point <= 0x2CEAF) || // CJK統合漢字拡張E
        (code_point >= 0x2CEB0 && code_point <= 0x2EBEF) || // CJK統合漢字拡張F
        (code_point >= 0x30000 && code_point <= 0x3134F) || // CJK統合漢字拡張G
        (code_point >= 0x3040 && code_point <= 0x309F) ||  // ひらがな
        (code_point >= 0x30A0 && code_point <= 0x30FF) ||  // カタカナ
        (code_point >= 0xFF66 && code_point <= 0xFF9F) ||  // 半角カタカナ
        (code_point >= 0x3100 && code_point <= 0x312F) ||  // 漢字注音符号（台湾）
        (code_point >= 0x31F0 && code_point <= 0x31FF) ||  // カタカナ拡張
        (code_point >= 0x3130 && code_point <= 0x318F) ||  // ハングル互換字母
        (code_point >= 0xAC00 && code_point <= 0xD7AF)) {  // ハングル音節
        return true;
    }
    
    return false;
}

/**
 * @brief 禁則文字かどうかを判定する
 * @param code_point Unicodeコードポイント
 * @param prohibit_chars 禁則文字配列
 * @return 禁則文字ならtrue、それ以外ならfalse
 */
static bool is_prohibit_char(uint32_t code_point, const uint32_t* prohibit_chars) {
    for (int i = 0; prohibit_chars[i] != 0; i++) {
        if (code_point == prohibit_chars[i]) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 行頭禁止文字かどうかを判定する
 */
static bool is_line_head_prohibit(uint32_t code_point) {
    return is_prohibit_char(code_point, LINE_HEAD_PROHIBIT_CHARS);
}

/**
 * @brief 行末禁止文字かどうかを判定する
 */
static bool is_line_tail_prohibit(uint32_t code_point) {
    return is_prohibit_char(code_point, LINE_TAIL_PROHIBIT_CHARS);
}

/**
 * @brief 単一の文字を描画する
 */
int epd_text_draw_char(EPDWrapper* wrapper, int x, int y, uint32_t code_point, const EPDTextConfig* config) {
    if (wrapper == NULL || config == NULL || config->font == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for drawing character");
        return 0;
    }
    
    // フォントから文字情報を取得
    const FontCharInfo* char_info = epd_text_find_char(config->font, code_point);
    if (char_info == NULL) {
        ESP_LOGW(TAG, "Character U+%lx not found in font", code_point);
        // 代替文字（□）を表示するか、スキップする
        return config->font->size / 2; // 文字幅の代わりにフォントサイズの半分を返す
    }
    
    // 文字のビットマップデータの取得
    const uint8_t* bitmap = config->font->bitmap_data + char_info->data_offset;
    
    // 回転が必要な場合の処理（縦書きモードで非CJK文字）
    bool need_rotation = config->vertical && config->rotate_non_cjk && !epd_text_is_cjk(code_point);
    
    // 文字の描画位置を計算（alignment適用）
    int draw_x = x;
    int draw_y = y;
    
    // 文字の幅と高さ
    int char_width = char_info->width;
    int char_height = config->font->max_height;
    
    // 縦書きモードの場合の座標調整
    if (config->vertical) {
        if (need_rotation) {
            // 非CJK文字を回転する場合、中央に配置
            draw_x = x - (char_height / 2);
            draw_y = y;
        } else {
            // CJK文字は縦に並べる
            draw_x = x - (char_width / 2);
            draw_y = y;
        }
    }
    
    // 描画ピクセルのカラー計算（EPDは4bitグレースケール）
    uint8_t text_color = config->text_color & 0x0F;
    uint8_t bg_color = config->bg_transparent ? 0xFF : (config->bg_color & 0x0F);
    
    // 文字の背景を描画（透明でなければ）
    if (!config->bg_transparent) {
        if (need_rotation) {
            // 90度回転した背景
            epd_wrapper_fill_rect(wrapper, draw_x, draw_y, char_height, char_width, bg_color);
        } else {
            epd_wrapper_fill_rect(wrapper, draw_x, draw_y, char_width, char_height, bg_color);
        }
    }
    
    // 文字ビットマップのサイズ（ビットマップはモノクロで1ピクセル1ビット）
    int bitmap_width = char_info->img_width;
    int bitmap_height = char_info->img_height;
    
    // 縦書きで回転が必要な場合
    if (need_rotation) {
        // ビットマップを90度回転して描画（時計回りに90度回転）
        for (int by = 0; by < bitmap_height; by++) {
            for (int bx = 0; bx < bitmap_width; bx++) {
                // ビットマップのピクセル位置を計算
                int byte_pos = (by * ((bitmap_width + 7) / 8)) + (bx / 8);
                int bit_pos = 7 - (bx % 8); // MSBが左端
                
                // ピクセル値を取得（0または1）
                uint8_t pixel = (bitmap[byte_pos] >> bit_pos) & 0x01;
                
                if (pixel) {
                    // 90度回転した位置に描画（時計回りに90度）
                    int rx = draw_x + by;
                    int ry = draw_y + (bitmap_width - bx - 1);
                    
                    // 太字効果が有効なら周囲にも描画
                    if (config->bold) {
                        for (int dx = -1; dx <= 1; dx++) {
                            for (int dy = -1; dy <= 1; dy++) {
                                if (dx == 0 && dy == 0) continue; // 中心は後で描画
                                epd_wrapper_draw_pixel(wrapper, rx + dx, ry + dy, text_color);
                            }
                        }
                    }
                    
                    // ピクセルを描画
                    epd_wrapper_draw_pixel(wrapper, rx, ry, text_color);
                }
            }
        }
        
        // 下線を描画（回転考慮）
        if (config->underline) {
            epd_wrapper_draw_line(wrapper, 
                draw_x, draw_y + bitmap_width + 2, 
                draw_x + bitmap_height, draw_y + bitmap_width + 2, 
                text_color);
        }
        
        // 縦書きで回転した場合は高さが文字幅になる
        return char_height;
    } else {
        // 通常の描画（回転なし）
        for (int by = 0; by < bitmap_height; by++) {
            for (int bx = 0; bx < bitmap_width; bx++) {
                int byte_pos = (by * ((bitmap_width + 7) / 8)) + (bx / 8);
                int bit_pos = 7 - (bx % 8); // MSBが左端
                
                uint8_t pixel = (bitmap[byte_pos] >> bit_pos) & 0x01;
                
                if (pixel) {
                    // 描画位置を計算
                    int px = draw_x + bx;
                    int py = draw_y + by;
                    
                    // 太字効果が有効なら周囲にも描画
                    if (config->bold) {
                        for (int dx = -1; dx <= 1; dx++) {
                            for (int dy = -1; dy <= 1; dy++) {
                                if (dx == 0 && dy == 0) continue; // 中心は後で描画
                                epd_wrapper_draw_pixel(wrapper, px + dx, py + dy, text_color);
                            }
                        }
                    }
                    
                    // ピクセルを描画
                    epd_wrapper_draw_pixel(wrapper, px, py, text_color);
                }
            }
        }
        
        // 下線を描画
        if (config->underline) {
            epd_wrapper_draw_line(wrapper, 
                draw_x, draw_y + bitmap_height + 2, 
                draw_x + bitmap_width, draw_y + bitmap_height + 2, 
                text_color);
        }
        
        // 文字幅を返す（次の文字の描画位置計算に使用）
        return char_width + config->char_spacing;
    }
}
/**
 * @brief テキストの描画幅を計算する
 */
int epd_text_calc_width(const char* text, const EPDTextConfig* config) {
    if (text == NULL || config == NULL || config->font == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for calculating text width");
        return 0;
    }
    
    int width = 0;
    int max_width = 0;
    
    // テキストを1文字ずつ処理
    const char* p = text;
    uint32_t code_point;
    
    while ((code_point = epd_text_utf8_next_char(&p)) != 0) {
        // 改行文字の処理
        if (code_point == '\n') {
            if (!config->vertical) {
                // 横書きモードの場合、最大幅を更新して次の行へ
                if (width > max_width) {
                    max_width = width;
                }
                width = 0;
            }
            continue;
        }
        
        // タブ文字の処理
        if (code_point == '\t') {
            // タブを4スペース分として処理
            const FontCharInfo* space_info = epd_text_find_char(config->font, ' ');
            if (space_info) {
                width += (space_info->width + config->char_spacing) * 4;
            } else {
                width += (config->font->size / 2 + config->char_spacing) * 4;
            }
            continue;
        }
        
        // 文字情報を取得
        const FontCharInfo* char_info = epd_text_find_char(config->font, code_point);
        if (char_info) {
            // 文字幅を加算
            width += char_info->width + config->char_spacing;
        } else {
            // フォントにない文字の場合、フォントサイズの半分を仮の幅とする
            width += config->font->size / 2 + config->char_spacing;
        }
    }
    
    // 最終的な幅を返す
    if (!config->vertical) {
        return width > max_width ? width : max_width;
    } else {
        // 縦書きの場合は文字列の長さが縦の長さになる
        return width;
    }
}

/**
 * @brief ルビ付きテキストを描画する
 */
int epd_text_draw_ruby(EPDWrapper* wrapper, int x, int y, const char* text, const char* ruby, const EPDTextConfig* config) {
    if (wrapper == NULL || text == NULL || ruby == NULL || config == NULL || config->font == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for drawing text with ruby");
        return 0;
    }
    
    // ルビ機能が無効な場合は、通常のテキスト描画
    if (!config->enable_ruby || config->ruby_font == NULL) {
        ESP_LOGW(TAG, "Ruby drawing is disabled or ruby font is not set");
        return epd_text_draw_string(wrapper, x, y, text, config);
    }
    
    ESP_LOGD(TAG, "Drawing text with ruby: '%s' (ruby: '%s') at (%d,%d)", text, ruby, x, y);
    
    // 親文字とルビの幅を計算
    int text_width = epd_text_calc_width(text, config);
    
    // ルビ用の設定をコピー
    EPDTextConfig ruby_config = *config;
    ruby_config.font = config->ruby_font;
    ruby_config.enable_ruby = false; // ルビのルビは描画しない
    
    int ruby_width = epd_text_calc_width(ruby, &ruby_config);
    
    // 描画開始位置
    int text_x = x;
    int text_y = y;
    int ruby_x = x;
    int ruby_y = y;
    
    // 配置調整（横書き/縦書き）
    if (!config->vertical) {
        // 横書きモード
        // ルビは親文字の上に配置
        ruby_y = y - config->ruby_font->max_height - config->ruby_offset;
        
        // ルビと親文字の幅が異なる場合の調整
        if (ruby_width != text_width) {
            if (config->alignment == EPD_TEXT_ALIGN_CENTER) {
                // 中央揃え
                ruby_x = x + (text_width - ruby_width) / 2;
            } else if (config->alignment == EPD_TEXT_ALIGN_RIGHT) {
                // 右揃え
                ruby_x = x + (text_width - ruby_width);
            }
        }
    } else {
        // 縦書きモード
        // ルビは親文字の右に配置
        ruby_x = x + config->ruby_offset;
        
        // ルビと親文字の高さが異なる場合の調整
        if (ruby_width != text_width) {
            if (config->alignment == EPD_TEXT_ALIGN_CENTER) {
                // 中央揃え
                ruby_y = y + (text_width - ruby_width) / 2;
            } else if (config->alignment == EPD_TEXT_ALIGN_RIGHT) {
                // 下揃え
                ruby_y = y + (text_width - ruby_width);
            }
        }
    }
    
    // 先にルビを描画
    epd_text_draw_string(wrapper, ruby_x, ruby_y, ruby, &ruby_config);
    
    // 次に親文字を描画
    return epd_text_draw_string(wrapper, text_x, text_y, text, config);
}

/**
 * @brief 複数行のテキストを描画する
 */
int epd_text_draw_multiline(EPDWrapper* wrapper, int x, int y, const char* text, const EPDTextConfig* config) {
    if (wrapper == NULL || text == NULL || config == NULL || config->font == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for drawing multiline text");
        return 0;
    }
    
    ESP_LOGD(TAG, "Drawing multiline text at (%d,%d)", x, y);
    
    // 折り返し幅が設定されていない場合は、通常の描画処理
    if (config->wrap_width <= 0) {
        epd_text_draw_string(wrapper, x, y, text, config);
        return 1; // 1行として扱う
    }
    
    int line_count = 0;
    int cur_x = x;
    int cur_y = y;
    
    // 行バッファ
    char* line_buffer = (char*)heap_caps_malloc(strlen(text) + 1, MALLOC_CAP_8BIT);
    if (line_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for line buffer");
        return 0;
    }
    
    const char* p = text;
    const char* line_start = text;
    const char* last_break = text;
    int line_width = 0;
    
    uint32_t code_point;
    int char_width;
    bool line_broken = false;
    
    while (1) {
        // 次の文字を取得
        const char* current_pos = p;
        code_point = epd_text_utf8_next_char(&p);
        
        if (code_point == 0 || code_point == '\n') {
            // 文字列終端または改行
            line_broken = true;
        } else {
            // 文字の幅を取得
            const FontCharInfo* char_info = epd_text_find_char(config->font, code_point);
            if (char_info) {
                char_width = char_info->width + config->char_spacing;
            } else {
                char_width = config->font->size / 2 + config->char_spacing;
            }
            
            // 行幅を更新
            line_width += char_width;
            
            // スペースの場合は単語区切りとして記録
            if (code_point == ' ' || code_point == '\t') {
                last_break = current_pos;
            }
            
            // 折り返し幅を超えたかチェック
            if (line_width > config->wrap_width) {
                // 単語区切りがあれば、そこまでを1行として描画
                if (last_break > line_start) {
                    int len = last_break - line_start;
                    memcpy(line_buffer, line_start, len);
                    line_buffer[len] = '\0';
                    
                    // 行を描画
                    epd_text_draw_string(wrapper, cur_x, cur_y, line_buffer, config);
                    line_count++;
                    
                    // 次の行の開始位置を更新
                    line_start = last_break;
                    if (*last_break == ' ' || *last_break == '\t') {
                        line_start++; // スペース・タブを飛ばす
                    }
                    
                    // 次の行の位置を更新
                    if (!config->vertical) {
                        cur_y += config->font->max_height + config->line_spacing;
                    } else {
                        cur_x -= config->font->max_height + config->line_spacing;
                    }
                    
                    // 行幅をリセット
                    p = line_start;
                    line_width = 0;
                    last_break = line_start;
                } else {
                    // 単語区切りがない場合は、現在位置で強制的に折り返し
                    int len = current_pos - line_start;
                    memcpy(line_buffer, line_start, len);
                    line_buffer[len] = '\0';
                    
                    // 行を描画
                    epd_text_draw_string(wrapper, cur_x, cur_y, line_buffer, config);
                    line_count++;
                    
                    // 次の行の開始位置を更新
                    line_start = current_pos;
                    
                    // 次の行の位置を更新
                    if (!config->vertical) {
                        cur_y += config->font->max_height + config->line_spacing;
                    } else {
                        cur_x -= config->font->max_height + config->line_spacing;
                    }
                    
                    // 行幅をリセット
                    line_width = char_width;
                    last_break = current_pos;
                }
            }
        }
        
        // 改行または文字列終端の処理
        if (line_broken) {
            // 現在までのテキストを1行として描画
            int len = current_pos - line_start;
            if (len > 0) {
                memcpy(line_buffer, line_start, len);
                line_buffer[len] = '\0';
                
                // 行を描画
                epd_text_draw_string(wrapper, cur_x, cur_y, line_buffer, config);
                line_count++;
            }
            
            // 改行の場合は次の行へ
            if (code_point == '\n' && *p) {
                if (!config->vertical) {
                    cur_y += config->font->max_height + config->line_spacing;
                } else {
                    cur_x -= config->font->max_height + config->line_spacing;
                }
                
                line_start = p;
                last_break = p;
                line_width = 0;
                line_broken = false;
            } else {
                // 文字列終端ならループを抜ける
                break;
            }
        }
    }
    
    // 行バッファを解放
    heap_caps_free(line_buffer);
    
    return line_count;
}

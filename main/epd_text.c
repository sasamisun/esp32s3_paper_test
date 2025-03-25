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
 
 /**
  * @brief テキスト描画設定を初期化する
  * @param config 初期化する設定構造体へのポインタ
  * @param font 使用するフォント情報
  */
 void epd_text_config_init(EPDTextConfig* config, const FontInfo* font) {
     if (config == NULL) {
         ESP_LOGE(TAG, "Invalid config pointer");
         return;
     }
 
     // 構造体をゼロクリア
     memset(config, 0, sizeof(EPDTextConfig));
 
     // フォント関連の設定
     config->font = font;
     config->text_color = 0x00;        // デフォルトで黒色（0）
     config->bold = false;             // デフォルトで太字なし
 
     // レイアウト関連の設定
     config->vertical = false;         // デフォルトで横書き
     config->line_spacing = 4;         // デフォルト行間
     config->char_spacing = 0;         // デフォルト文字間隔
     config->alignment = EPD_TEXT_ALIGN_LEFT; // デフォルトで左揃え
     config->rotate_non_cjk = true;    // 縦書き時に非CJK文字を回転
 
     // 装飾関連の設定
     config->bg_color = 0x0F;          // デフォルトで白色（15）
     config->bg_transparent = true;    // デフォルトで背景透明
     config->underline = false;        // デフォルトで下線なし
 
     // ルビ関連の設定
     config->enable_ruby = false;      // デフォルトでルビ無効
     config->ruby_font = NULL;         // ルビ用フォントは未設定
     config->ruby_offset = 2;          // デフォルトルビオフセット
 
     // その他の設定
     config->wrap_width = 0;           // デフォルトで折り返しなし
     config->enable_kerning = false;   // デフォルトでカーニング無効
 
     ESP_LOGI(TAG, "Text config initialized with font size %d", font ? font->size : 0);
 }
 
 /**
  * @brief フォントからコードポイントに対応する文字情報を検索する
  * @param font フォント情報
  * @param code_point 検索するUnicodeコードポイント
  * @return 該当する文字情報のポインタ。見つからない場合はNULL
  */
 const FontCharInfo* epd_text_find_char(const FontInfo* font, uint32_t code_point) {
     if (font == NULL || font->chars == NULL || font->chars_count == 0) {
         return NULL;
     }
 
     // バイナリサーチでコードポイントを検索
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
 
     // 見つからなかった場合
     ESP_LOGD(TAG, "Character U+%08lX not found in font", code_point);
     return NULL;
 }
 
 /**
  * @brief UTF-8テキストから次の文字のUnicodeコードポイントを取得する
  * @param text UTF-8テキストへのポインタのポインタ（処理後、ポインタは次の文字位置に進む）
  * @return 次の文字のUnicodeコードポイント。文字列終端の場合は0
  */
 uint32_t epd_text_utf8_next_char(const char** text) {
     if (text == NULL || *text == NULL || **text == '\0') {
         return 0;
     }
 
     const unsigned char* s = (const unsigned char*)(*text);
     uint32_t code_point = 0;
     int bytes_to_read = 0;
 
     // UTF-8エンコーディングを解析
     if (*s < 0x80) {
         // 1バイト文字 (ASCII)
         code_point = *s;
         bytes_to_read = 1;
     } else if ((*s & 0xE0) == 0xC0) {
         // 2バイト文字
         code_point = (*s & 0x1F);
         bytes_to_read = 2;
     } else if ((*s & 0xF0) == 0xE0) {
         // 3バイト文字
         code_point = (*s & 0x0F);
         bytes_to_read = 3;
     } else if ((*s & 0xF8) == 0xF0) {
         // 4バイト文字
         code_point = (*s & 0x07);
         bytes_to_read = 4;
     } else {
         // 無効なUTF-8シーケンス - スキップして次へ
         (*text)++;
         ESP_LOGW(TAG, "Invalid UTF-8 sequence");
         return 0xFFFD; // Replacement character
     }
 
     // 残りのバイトを読み込む
     s++;
     for (int i = 1; i < bytes_to_read; i++) {
         if ((*s & 0xC0) != 0x80) {
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
 bool epd_text_is_cjk(uint32_t code_point) {
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
         (code_point >= 0x2F800 && code_point <= 0x2FA1F)) { // CJK互換漢字補助
         return true;
     }
 
     // 日本語特有
     if ((code_point >= 0x3040 && code_point <= 0x309F) ||   // ひらがな
         (code_point >= 0x30A0 && code_point <= 0x30FF) ||   // カタカナ
         (code_point >= 0x31F0 && code_point <= 0x31FF)) {   // カタカナ拡張
         return true;
     }
 
     // 韓国語（ハングル）
     if ((code_point >= 0xAC00 && code_point <= 0xD7AF) ||   // ハングル音節
         (code_point >= 0x1100 && code_point <= 0x11FF) ||   // ハングル字母
         (code_point >= 0x3130 && code_point <= 0x318F)) {   // ハングル互換字母
         return true;
     }
 
     // その他のCJK関連文字
     if ((code_point >= 0x3000 && code_point <= 0x303F) ||   // CJK記号と句読点
         (code_point >= 0xFF00 && code_point <= 0xFFEF)) {   // 全角形と半角形
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
int epd_text_draw_char(EPDWrapper* wrapper, int x, int y, uint32_t code_point, const EPDTextConfig* config) {
    if (wrapper == NULL || config == NULL || config->font == NULL) {
        ESP_LOGE(TAG, "Invalid parameters for drawing character");
        return 0;
    }

    // フォントから文字情報を検索
    const FontCharInfo* char_info = epd_text_find_char(config->font, code_point);
    if (char_info == NULL) {
        ESP_LOGW(TAG, "Character U+%04lX not found in font", code_point);
        return 0;
    }

    // ビットマップデータのポインタを取得
    const uint8_t* bitmap = config->font->bitmap_data + char_info->data_offset;
    
    // 描画位置の調整
    int draw_x = x;
    int draw_y = y;
    
    // 縦書きモードの場合、座標を調整
    bool rotate_this_char = false;
    if (config->vertical) {
        // CJK文字でない場合、文字を回転させる
        if (!epd_text_is_cjk(code_point) && config->rotate_non_cjk) {
            rotate_this_char = true;
        } else {
            // 縦書きでCJK文字または回転しない設定の場合、Y座標を中央揃えする
            draw_y = y - (char_info->width / 2);
        }
    }

    // 文字の幅と高さを取得
    int char_width = char_info->img_width;
    int char_height = char_info->img_height;
    
    ESP_LOGI(TAG, "Drawing character U+%04lX at (%d,%d), size: %dx%d, data_offset: %lu", 
            code_point, draw_x, draw_y, char_width, char_height, char_info->data_offset);

    // 太字表示の場合のオフセット
    int bold_offset = config->bold ? 1 : 0;
    
    // 背景を描画（透明でない場合）
    if (!config->bg_transparent) {
        // 背景の描画領域
        int bg_x = draw_x;
        int bg_y = draw_y;
        int bg_width = char_width;
        int bg_height = char_height;
        
        // 縦書きで回転する文字の場合
        if (config->vertical && rotate_this_char) {
            // 回転した場合の領域を考慮（単純化のため縦横入れ替え）
            bg_width = char_height;
            bg_height = char_width;
        }
        
        // 背景を塗りつぶす
        epd_wrapper_fill_rect(wrapper, bg_x, bg_y, bg_width, bg_height, config->bg_color);
    }

    // ビットマップデータの描画
    if (rotate_this_char) {
        // 非CJK文字を90度回転して描画（縦書きモード）
        // 回転用の一時バッファを確保
        int rotated_size = ((char_height + 1) / 2) * char_width; // 4bit/pixelのバイト数計算
        uint8_t* rotated_data = heap_caps_malloc(rotated_size, MALLOC_CAP_8BIT);
        if (rotated_data == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for rotated character");
            return char_info->width;
        }
        
        // バッファを0で初期化
        memset(rotated_data, 0, rotated_size);
        
        // 90度回転（時計回り）- 簡易版
        for (int src_y = 0; src_y < char_height; src_y++) {
            for (int src_x = 0; src_x < char_width; src_x++) {
                // 元のピクセル位置と値を計算
                int src_pos = src_y * ((char_width + 1) / 2) + (src_x / 2);
                uint8_t src_pixel;
                
                if (src_x % 2 == 0) {
                    src_pixel = (bitmap[src_pos] & 0x0F);
                } else {
                    src_pixel = (bitmap[src_pos] & 0xF0) >> 4;
                }
                
                // 新しい位置（90度回転）
                int new_x = char_height - 1 - src_y;
                int new_y = src_x;
                
                // 新しい位置にピクセルを設定
                int dst_pos = new_y * ((char_height + 1) / 2) + (new_x / 2);
                
                if (new_x % 2 == 0) {
                    rotated_data[dst_pos] = (rotated_data[dst_pos] & 0xF0) | src_pixel;
                } else {
                    rotated_data[dst_pos] = (rotated_data[dst_pos] & 0x0F) | (src_pixel << 4);
                }
            }
        }
        
        // 太字表示の場合、同じ文字を1ピクセル右にもう一度描画
        for (int by = 0; by < char_width; by++) {
            for (int bx = 0; bx < char_height; bx++) {
                int pixel_pos = by * ((char_height + 1) / 2) + (bx / 2);
                uint8_t pixel;
                
                if (bx % 2 == 0) {
                    pixel = (rotated_data[pixel_pos] & 0x0F);
                } else {
                    pixel = (rotated_data[pixel_pos] & 0xF0) >> 4;
                }
                
                // 色調整（必要な場合）
                if (pixel != 0 && pixel != config->bg_color) {
                    // 0（黒）と15（白）の間でテキスト色を設定
                    pixel = config->text_color;
                    
                    // ピクセルを描画
                    int px = draw_x + bx;
                    int py = draw_y + by;
                    
                    // 実際のピクセル描画（EPDラッパー使用）
                    epd_wrapper_draw_pixel(wrapper, px, py, pixel);
                    
                    // 太字処理
                    if (config->bold && bx < char_height - 1) {
                        epd_wrapper_draw_pixel(wrapper, px + bold_offset, py, pixel);
                    }
                }
            }
        }
        
        // 回転用の一時バッファを解放
        heap_caps_free(rotated_data);
        
    } else {
        // 通常の描画（回転なし）
        for (int by = 0; by < char_height; by++) {
            for (int bx = 0; bx < char_width; bx++) {
                // ビットマップデータから4bitピクセル値を取得
                int bitmap_pos = by * ((char_width + 1) / 2) + (bx / 2);
                uint8_t pixel;
                
                if (bx % 2 == 0) {
                    pixel = (bitmap[bitmap_pos] & 0x0F);
                } else {
                    pixel = (bitmap[bitmap_pos] & 0xF0) >> 4;
                }
                
                // 色調整（必要な場合）
                if (pixel != 0 && pixel != config->bg_color) {
                    // 0（黒）と15（白）の間でテキスト色を設定
                    pixel = config->text_color;
                    
                    // ピクセルを描画
                    int px = draw_x + bx;
                    int py = draw_y + by;
                    
                    // 実際のピクセル描画（EPDラッパー使用）
                    uint8_t color_8bit = (pixel << 4) | pixel; // 4ビット値を8ビットに拡張
                    epd_draw_pixel(px, py, color_8bit, wrapper->framebuffer);
                    
                    // 太字処理
                    if (config->bold && bx < char_width - 1) {
                        epd_draw_pixel(px + bold_offset, py, color_8bit, wrapper->framebuffer);
                    }
                }
            }
        }
    }
    
    // 下線描画（有効な場合）
    if (config->underline) {
        int line_y = draw_y + char_height + 1;
        int line_width = char_info->width;
        
        // 縦書きモードで回転する文字の場合
        if (config->vertical && rotate_this_char) {
            // 縦書きの下線は文字の右側に描画
            int line_x = draw_x + char_height + 1;
            epd_wrapper_draw_line(wrapper, line_x, draw_y, line_x, draw_y + char_width, config->text_color);
        } else {
            // 横書きの下線は文字の下に描画
            epd_wrapper_draw_line(wrapper, draw_x, line_y, draw_x + line_width, line_y, config->text_color);
        }
    }
    
    // 返り値：描画した文字の幅
    return char_info->width + config->char_spacing;
}
 
 int epd_text_draw_string(EPDWrapper* wrapper, int x, int y, const char* text, const EPDTextConfig* config) {
     ESP_LOGW(TAG, "epd_text_draw_string not fully implemented yet");
     return 0;
 }
 
 int epd_text_draw_multiline(EPDWrapper* wrapper, int x, int y, const char* text, const EPDTextConfig* config) {
     ESP_LOGW(TAG, "epd_text_draw_multiline not fully implemented yet");
     return 0;
 }
 
 int epd_text_draw_ruby(EPDWrapper* wrapper, int x, int y, const char* text, const char* ruby, const EPDTextConfig* config) {
     ESP_LOGW(TAG, "epd_text_draw_ruby not fully implemented yet");
     return 0;
 }
 
 int epd_text_calc_width(const char* text, const EPDTextConfig* config) {
     ESP_LOGW(TAG, "epd_text_calc_width not fully implemented yet");
     return 0;
 }
 
 int epd_text_calc_height(const char* text, const EPDTextConfig* config) {
     ESP_LOGW(TAG, "epd_text_calc_height not fully implemented yet");
     return 0;
 }
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
 
 // 以下の関数はスタブとして実装（後で実装する）
 int epd_text_draw_char(EPDWrapper* wrapper, int x, int y, uint32_t code_point, const EPDTextConfig* config) {
     ESP_LOGW(TAG, "epd_text_draw_char not fully implemented yet");
     return 0;
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
/**
 * @file epd_text.c
 * @brief 電子ペーパーディスプレイのテキスト描画機能の実装
 */

 #include <stdio.h>
 #include <string.h>
 #include <stdlib.h>
 #include "esp_log.h"
 #include "epd_text.h"
 
 static const char *TAG = "epd_text";
 
 /**
  * @brief テキスト描画設定をデフォルト値で初期化
  * @param config 初期化するテキスト設定構造体へのポインタ
  * @param font 使用するフォント
  */
 void epd_text_config_init(EPDTextConfig* config, const FontInfo* font) {
     if (config == NULL) return;
     
     memset(config, 0, sizeof(EPDTextConfig));
     
     // デフォルト設定
     config->font = font;
     config->text_color = 0;  // 黒色
     config->bold = false;
     
     config->vertical = false;
     config->line_spacing = 2;
     config->char_spacing = 0;
     config->alignment = EPD_TEXT_ALIGN_LEFT;
     config->rotate_non_cjk = true;
     
     config->bg_color = 0xFF;  // 白色
     config->bg_transparent = true;
     config->underline = false;
     
     config->enable_ruby = false;
     config->ruby_font = NULL;
     config->ruby_offset = 2;
     
     config->wrap_width = 0;  // 折り返しなし
     config->enable_kerning = false;
 }
 
 /**
  * @brief フォント内で指定された文字を検索（バイナリサーチ）
  * @param font フォント情報
  * @param code_point 検索するUnicode値
  * @return 文字情報へのポインタ（見つからない場合はNULL）
  */
 const FontCharInfo* epd_text_find_char(const FontInfo* font, uint32_t code_point) {
     if (font == NULL || font->chars == NULL || font->chars_count == 0) {
         return NULL;
     }
     
     // バイナリサーチで文字を検索
     int low = 0;
     int high = font->chars_count - 1;
     
     while (low <= high) {
         int mid = (low + high) / 2;
         uint32_t mid_code = font->chars[mid].code_point;
         
         if (mid_code < code_point) {
             low = mid + 1;
         } else if (mid_code > code_point) {
             high = mid - 1;
         } else {
             // 見つかった
             return &font->chars[mid];
         }
     }
     
     // 見つからなかった
     return NULL;
 }
 
 /**
  * @brief UTF-8文字列から次の1文字を取得し、ポインタを進める
  * @param text UTF-8文字列へのポインタへのポインタ（ポインタが更新される）
  * @return Unicode値（UTF-32）
  */
 uint32_t epd_text_utf8_next_char(const char** text) {
     const unsigned char* s = (const unsigned char*)*text;
     uint32_t ch = 0;
     
     if (!s || !*s) return 0;  // 終端またはNULL
     
     if (*s < 0x80) {  // 1バイト文字
         ch = *s++;
     } else if (*s < 0xE0) {  // 2バイト文字
         if (*(s+1)) {  // 2バイト目が存在するか確認
             ch = ((*s & 0x1F) << 6) | (*(s+1) & 0x3F);
             s += 2;
         } else {
             // 不正なUTF-8シーケンス
             ch = *s++;
         }
     } else if (*s < 0xF0) {  // 3バイト文字
         if (*(s+1) && *(s+2)) {  // 2,3バイト目が存在するか確認
             ch = ((*s & 0x0F) << 12) | ((*(s+1) & 0x3F) << 6) | (*(s+2) & 0x3F);
             s += 3;
         } else {
             // 不正なUTF-8シーケンス
             ch = *s++;
         }
     } else if (*s < 0xF8) {  // 4バイト文字
         if (*(s+1) && *(s+2) && *(s+3)) {  // 2,3,4バイト目が存在するか確認
             ch = ((*s & 0x07) << 18) | ((*(s+1) & 0x3F) << 12) | 
                  ((*(s+2) & 0x3F) << 6) | (*(s+3) & 0x3F);
             s += 4;
         } else {
             // 不正なUTF-8シーケンス
             ch = *s++;
         }
     } else {
         // 不正なUTF-8シーケンス
         ch = *s++;
     }
     
     *text = (const char*)s;
     return ch;
 }
 
 /**
  * @brief 1ピクセル = 1ビットのビットマップを描画
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x 描画X座標
  * @param y 描画Y座標
  * @param char_info 文字情報
  * @param font フォント情報
  * @param color 描画色
  * @param bg_color 背景色
  * @param bg_transparent 背景透過フラグ
  */
 static void draw_bitmap_1bit(EPDWrapper* wrapper, int x, int y, 
                               const FontCharInfo* char_info, const FontInfo* font,
                               uint8_t color, uint8_t bg_color, bool bg_transparent) {
     const uint8_t* bitmap = &font->bitmap_data[char_info->data_offset];
     
     uint8_t img_width = char_info->img_width;
     uint8_t img_height = char_info->img_height;
     
     // バイト境界に合わせたバイト幅を計算（8ピクセル = 1バイト）
     uint8_t width_bytes = (img_width + 7) / 8;
     
     for (int row = 0; row < img_height; row++) {
         for (int col = 0; col < img_width; col++) {
             // ビットマップデータ内の位置を計算
             int byte_idx = row * width_bytes + (col / 8);
             int bit_offset = 7 - (col % 8);  // MSBファースト
             
             // ビットの値を取得（0か1）
             uint8_t bit_value = (bitmap[byte_idx] >> bit_offset) & 0x01;
             
             if (bit_value) {
                 // ピクセルが立っている場合は文字色で描画
                 epd_draw_pixel(x + col, y + row, color, wrapper->framebuffer);
             } else if (!bg_transparent) {
                 // 背景を透過しない場合は背景色で描画
                 epd_draw_pixel(x + col, y + row, bg_color, wrapper->framebuffer);
             }
         }
     }
 }
 
 /**
  * @brief 単一文字を描画
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x 描画開始X座標
  * @param y 描画開始Y座標
  * @param code_point 描画する文字のUnicode値
  * @param config テキスト描画設定
  * @return 描画した文字幅
  */
 int epd_text_draw_char(EPDWrapper* wrapper, int x, int y, uint32_t code_point, const EPDTextConfig* config) {
     if (wrapper == NULL || config == NULL || config->font == NULL) {
         ESP_LOGE(TAG, "Invalid parameters for epd_text_draw_char");
         return 0;
     }
     
     // 文字情報を取得
     const FontCharInfo* char_info = epd_text_find_char(config->font, code_point);
     if (char_info == NULL) {
         // 文字が見つからない場合、スペースを描画
         ESP_LOGW(TAG, "Character U+%04lX not found in font", code_point);
         return config->font->size / 2;  // スペースの幅として適当な値を返す
     }
     
     // 縦書きモードの場合
     if (config->vertical) {
         // ASCII文字（特に半角英数）を回転させるか
         bool should_rotate = config->rotate_non_cjk && 
                             ((code_point >= 0x0020 && code_point <= 0x007F) ||
                              (code_point >= 0xFF01 && code_point <= 0xFF5E)); // 全角英数
         
         if (should_rotate) {
             // 90度回転して描画
             // この実装は単純化しています。実際には文字を回転する別のロジックが必要です
             y -= char_info->width;
         }
     }
     
     // ビットマップを描画
     draw_bitmap_1bit(wrapper, x, y, char_info, config->font, 
                      config->text_color, config->bg_color, config->bg_transparent);
     
     // 太字の場合は1ピクセル右にずらして再度描画してボールド効果を作る
     if (config->bold) {
         draw_bitmap_1bit(wrapper, x + 1, y, char_info, config->font, 
                          config->text_color, config->bg_color, true); // 背景は透過
     }
     
     // 下線が有効の場合は下線を描画
     if (config->underline) {
         int underline_y = y + config->font->max_height + 1;
         epd_draw_line(x, underline_y, x + char_info->width, underline_y, 
                       config->text_color, wrapper->framebuffer);
     }
     
     // 描画した文字の幅を返す
     return char_info->width;
 }
 
 /**
  * @brief UTF-8文字列を描画（1行のみ）
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x 描画開始X座標
  * @param y 描画開始Y座標
  * @param text 描画するUTF-8テキスト
  * @param config テキスト描画設定
  * @return 描画したテキスト幅（ピクセル単位）
  */
 int epd_text_draw_string(EPDWrapper* wrapper, int x, int y, const char* text, const EPDTextConfig* config) {
     if (wrapper == NULL || text == NULL || config == NULL || config->font == NULL) {
         ESP_LOGE(TAG, "Invalid parameters for epd_text_draw_string");
         return 0;
     }
     
     // 事前にテキスト幅を計算（テキスト配置のため）
     int text_width = 0;
     if (config->alignment != EPD_TEXT_ALIGN_LEFT) {
         text_width = epd_text_calc_width(text, config);
     }
     
     // 描画開始位置を調整（テキスト配置に基づく）
     int start_x = x;
     if (config->alignment == EPD_TEXT_ALIGN_CENTER) {
         start_x = x - text_width / 2;
     } else if (config->alignment == EPD_TEXT_ALIGN_RIGHT) {
         start_x = x - text_width;
     }
     
     // 現在の描画位置
     int current_x = start_x;
     int current_y = y;
     
     // 縦書きモードの場合
     if (config->vertical) {
         // 縦書きモードでは、X,Yの扱いが逆になる
         current_x = x;
         
         if (config->alignment == EPD_TEXT_ALIGN_CENTER) {
             current_y = y - text_width / 2;
         } else if (config->alignment == EPD_TEXT_ALIGN_RIGHT) {
             current_y = y - text_width;
         } else {
             current_y = y;
         }
     }
     
     // 各文字をループしながら描画
     const char* p = text;
     uint32_t code_point;
     
     while ((code_point = epd_text_utf8_next_char(&p)) != 0) {
         // 文字情報を取得
         const FontCharInfo* char_info = epd_text_find_char(config->font, code_point);
         if (char_info == NULL) {
             // 文字が見つからない場合はスキップ
             current_x += config->font->size / 2 + config->char_spacing; // スペースとして適当な幅を進める
             continue;
         }
         
         if (config->vertical) {
             // 縦書きモードでは、Y方向に文字を配置
             int char_width = epd_text_draw_char(wrapper, current_x, current_y, code_point, config);
             current_y += char_width + config->char_spacing;
         } else {
             // 横書きモードでは、X方向に文字を配置
             int char_width = epd_text_draw_char(wrapper, current_x, current_y, code_point, config);
             current_x += char_width + config->char_spacing;
         }
     }
     
     // 描画したテキストの全幅を返す
     if (config->vertical) {
         return current_y - y;
     } else {
         return current_x - start_x;
     }
 }
 
 /**
  * @brief テキストの描画幅を計算（実際には描画しない）
  * @param text 計算するUTF-8テキスト
  * @param config テキスト描画設定
  * @return テキストの描画幅（ピクセル単位）
  */
 int epd_text_calc_width(const char* text, const EPDTextConfig* config) {
     if (text == NULL || config == NULL || config->font == NULL) {
         return 0;
     }
     
     int total_width = 0;
     const char* p = text;
     uint32_t code_point;
     
     while ((code_point = epd_text_utf8_next_char(&p)) != 0) {
         const FontCharInfo* char_info = epd_text_find_char(config->font, code_point);
         if (char_info != NULL) {
             total_width += char_info->width;
         } else {
             // 文字が見つからない場合は適当な幅を加算
             total_width += config->font->size / 2;
         }
         
         // 最後の文字以外は文字間隔を追加
         if (*p != '\0') {
             total_width += config->char_spacing;
         }
     }
     
     return total_width;
 }
 
 /**
  * @brief テキストの描画高さを計算（実際には描画しない）
  * @param text 計算するUTF-8テキスト
  * @param config テキスト描画設定
  * @return テキストの描画高さ（ピクセル単位）
  */
 int epd_text_calc_height(const char* text, const EPDTextConfig* config) {
     if (text == NULL || config == NULL || config->font == NULL) {
         return 0;
     }
     
     if (config->vertical) {
         // 縦書きモードの場合、高さはテキスト幅と同等になる
         return epd_text_calc_width(text, config);
     } else {
         // 横書きモードの場合、行数に応じた高さを計算
         // 現在は1行のみのサポートなので、単純にフォント高さを返す
         return config->font->max_height;
     }
 }
 
 /**
  * @brief 複数行テキストを描画（折り返しあり）
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x 描画開始X座標
  * @param y 描画開始Y座標
  * @param text 描画するUTF-8テキスト
  * @param config テキスト描画設定
  * @return 描画した行数
  */
 int epd_text_draw_multiline(EPDWrapper* wrapper, int x, int y, const char* text, const EPDTextConfig* config) {
     if (wrapper == NULL || text == NULL || config == NULL || config->font == NULL) {
         ESP_LOGE(TAG, "Invalid parameters for epd_text_draw_multiline");
         return 0;
     }
     
     // 折り返し幅が設定されていない場合は単一行として描画
     if (config->wrap_width <= 0) {
         epd_text_draw_string(wrapper, x, y, text, config);
         return 1;
     }
     
     // 簡易的な折り返し処理
     // 実際には日本語の禁則処理なども考慮する必要がある
     
     const char* p = text;
     const char* line_start = text;
     int line_count = 0;
     int current_y = y;
     
     // 簡易的な行分割
     while (*p) {
         // 現在位置までの幅を計算
         int current_width = 0;
         const char* tmp = line_start;
         const char* last_break = NULL;
         uint32_t code_point;
         
         while (tmp <= p) {
             const char* prev = tmp;
             code_point = epd_text_utf8_next_char(&tmp);
             if (code_point == 0) break;
             
            // 空白または禁則文字かチェック（簡易版）
            // 「、」はU+3001、「。」はU+3002
            if (code_point == ' ' || code_point == 0x3001 || code_point == 0x3002) {
                last_break = prev;
            }
             
             // 現在の文字の幅を加算
             const FontCharInfo* char_info = epd_text_find_char(config->font, code_point);
             if (char_info != NULL) {
                 current_width += char_info->width + config->char_spacing;
             } else {
                 current_width += config->font->size / 2 + config->char_spacing;
             }
             
             // 改行文字の場合は強制的に改行
             if (code_point == '\n') {
                 p = prev;
                 break;
             }
             
             // 幅を超えたら、最後の改行可能位置で分割
             if (current_width > config->wrap_width) {
                 if (last_break != NULL) {
                     p = last_break;
                 }
                 break;
             }
         }
         
         // 1行分の文字列を抽出して描画
         size_t line_len = p - line_start;
         char* line_buf = malloc(line_len + 1);
         if (line_buf) {
             memcpy(line_buf, line_start, line_len);
             line_buf[line_len] = '\0';
             
             // 改行を除去
             char* newline = strchr(line_buf, '\n');
             if (newline) *newline = '\0';
             
             // 行を描画
             epd_text_draw_string(wrapper, x, current_y, line_buf, config);
             free(line_buf);
             
             // 次の行へ
             line_count++;
             current_y += config->font->max_height + config->line_spacing;
             
             // 次の行の開始位置を設定
             if (*p == '\n') p++;  // 改行文字をスキップ
             line_start = p;
         } else {
             ESP_LOGE(TAG, "Failed to allocate memory for line buffer");
             break;
         }
     }
     
     return line_count;
 }
 
 /**
  * @brief ルビ付きテキストを描画
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x 描画開始X座標
  * @param y 描画開始Y座標
  * @param text 描画するUTF-8テキスト（親文字）
  * @param ruby ルビ文字列
  * @param config テキスト描画設定
  * @return 描画したテキスト幅（ピクセル単位）
  */
 int epd_text_draw_ruby(EPDWrapper* wrapper, int x, int y, const char* text, const char* ruby, const EPDTextConfig* config) {
     if (wrapper == NULL || text == NULL || ruby == NULL || config == NULL || 
         config->font == NULL || !config->enable_ruby || config->ruby_font == NULL) {
         ESP_LOGE(TAG, "Invalid parameters for epd_text_draw_ruby");
         return 0;
     }
     
     // 親文字のテキスト幅を計算
     int text_width = epd_text_calc_width(text, config);
     
     // ルビ用の設定をコピー
     EPDTextConfig ruby_config;
     memcpy(&ruby_config, config, sizeof(EPDTextConfig));
     ruby_config.font = config->ruby_font;
     
     // ルビの幅を計算
     int ruby_width = epd_text_calc_width(ruby, &ruby_config);
     
     // 親文字とルビの配置を決定
     int ruby_x = x;
     int ruby_y;
     
     if (config->vertical) {
         // 縦書きモードでのルビ位置
         ruby_y = y;
         
         // ルビの配置（親文字の右側）
         ruby_x = x - config->ruby_offset - config->ruby_font->max_height;
     } else {
         // 横書きモードでのルビ位置（親文字の上）
         ruby_y = y - config->ruby_offset - config->ruby_font->max_height;
     }
     
     // テキスト配置の調整
     if (text_width > ruby_width) {
         // 親文字の方が長い場合、ルビを中央に配置
         if (config->vertical) {
             int diff = (text_width - ruby_width) / 2;
             ruby_y += diff;
         } else {
             int diff = (text_width - ruby_width) / 2;
             ruby_x += diff;
         }
     } else {
         // ルビの方が長い場合、親文字を中央に配置
         if (config->vertical) {
             int diff = (ruby_width - text_width) / 2;
             y += diff;
         } else {
             int diff = (ruby_width - text_width) / 2;
             x += diff;
         }
     }
     
     // ルビを描画
     epd_text_draw_string(wrapper, ruby_x, ruby_y, ruby, &ruby_config);
     
     // 親文字を描画
     return epd_text_draw_string(wrapper, x, y, text, config);
 }
/**
 * @file epd_text.h
 * @brief 電子ペーパーディスプレイ向けテキスト表示機能
 * 
 * UTF-8テキストを電子ペーパーディスプレイに表示するための機能を提供します。
 * 横書き・縦書き対応、ルビ表示、複数行テキスト対応など、
 * 豊富なタイポグラフィ機能をサポートします。
 */

 #ifndef EPD_TEXT_H
 #define EPD_TEXT_H
 
 #include <stdint.h>
 #include <stdbool.h>
 #include "epd_wrapper.h"
 
 /**
  * @brief フォント文字情報
  */
 typedef struct {
     uint32_t code_point;   // Unicode値（UTF-32）
     uint8_t width;         // 文字幅（ピクセル単位）
     uint32_t data_offset;  // ビットマップデータの開始位置
     uint8_t img_width;     // 画像全体の幅
     uint8_t img_height;    // 画像全体の高さ
 } FontCharInfo;
 
 /**
  * @brief フォント情報
  */
 typedef struct {
     uint8_t size;             // フォントの基本サイズ
     uint8_t max_height;       // 最大の文字高さ
     const char* style;        // フォントスタイル
     uint16_t chars_count;     // 収録文字数
     const FontCharInfo* chars; // 文字情報配列
     const uint8_t* bitmap_data; // ビットマップデータ配列
 } FontInfo;
 
 /**
  * @brief テキスト配置方向
  */
 typedef enum {
     EPD_TEXT_ALIGN_LEFT,     // 左揃え（横書き時）/ 上揃え（縦書き時）
     EPD_TEXT_ALIGN_CENTER,   // 中央揃え
     EPD_TEXT_ALIGN_RIGHT     // 右揃え（横書き時）/ 下揃え（縦書き時）
 } EPDTextAlignment;
 
 /**
  * @brief テキスト描画設定
  */
 typedef struct {
     // フォント関連
     const FontInfo* font;      // フォントデータ
     uint8_t text_color;        // テキスト色（0-15のグレースケール）
     bool bold;                 // 太字表示するか
     
     // レイアウト関連
     bool vertical;             // 縦書きモード
     int line_spacing;          // 行間（ピクセル単位）
     int char_spacing;          // 文字間隔（ピクセル単位）
     EPDTextAlignment alignment; // テキスト揃え方向
     bool rotate_non_cjk;       // 非CJK文字を縦書き時に回転するか
     
     // 装飾関連
     uint8_t bg_color;          // 背景色（0-15のグレースケール）
     bool bg_transparent;       // 背景を透明にするか
     bool underline;            // 下線を表示するか
     
     // ルビ関連
     bool enable_ruby;          // ルビを有効にするか
     const FontInfo* ruby_font; // ルビ用フォント
     int ruby_offset;           // ルビのオフセット（ピクセル単位）
     
     // その他の設定
     int wrap_width;            // テキスト折り返し幅（0の場合は折り返しなし）
     bool enable_kerning;       // カーニングを有効にするか
 } EPDTextConfig;
 
 /**
  * @brief テキスト描画設定を初期化する
  * @param config 初期化する設定構造体へのポインタ
  * @param font 使用するフォント情報
  */
 void epd_text_config_init(EPDTextConfig* config, const FontInfo* font);
 
 /**
  * @brief 単一の文字を描画する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x X座標
  * @param y Y座標
  * @param code_point 描画する文字のUnicodeコードポイント
  * @param config テキスト描画設定
  * @return 描画した文字の幅
  */
 int epd_text_draw_char(EPDWrapper* wrapper, int x, int y, uint32_t code_point, const EPDTextConfig* config);
 
 /**
  * @brief UTF-8文字列を描画する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x X座標
  * @param y Y座標
  * @param text 描画するUTF-8文字列
  * @param config テキスト描画設定
  * @return 描画した文字列の幅
  */
 int epd_text_draw_string(EPDWrapper* wrapper, int x, int y, const char* text, const EPDTextConfig* config);
 
/**
 * @brief 文字が行頭禁止文字かどうかを判定する
 * @param code_point 判定するUnicodeコードポイント
 * @return 行頭禁止文字の場合はtrue、それ以外はfalse
 */
bool epd_text_is_no_start_char(uint32_t code_point);

/**
 * @brief 文字が行末禁止文字かどうかを判定する
 * @param code_point 判定するUnicodeコードポイント
 * @return 行末禁止文字の場合はtrue、それ以外はfalse
 */
bool epd_text_is_no_end_char(uint32_t code_point);

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
int epd_text_draw_multiline(EPDWrapper* wrapper, int x, int y, EpdRect* rect, const char* text, const EPDTextConfig* config);
 
/**
  * @brief ルビ付きテキストを描画する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x X座標
  * @param y Y座標
  * @param text 親文字（描画するUTF-8文字列）
  * @param ruby ルビ（描画するUTF-8文字列）
  * @param config テキスト描画設定
  * @return 描画した文字列の幅
  */
 int epd_text_draw_ruby(EPDWrapper* wrapper, int x, int y, const char* text, const char* ruby, const EPDTextConfig* config);
 
 /**
  * @brief テキストの描画幅を計算する
  * @param text 計算するUTF-8文字列
  * @param config テキスト描画設定
  * @return テキストの描画幅（ピクセル単位）
  */
 int epd_text_calc_width(const char* text, const EPDTextConfig* config);
 
 /**
  * @brief テキストの描画高さを計算する
  * @param text 計算するUTF-8文字列
  * @param config テキスト描画設定
  * @return テキストの描画高さ（ピクセル単位）
  */
 int epd_text_calc_height(const char* text, const EPDTextConfig* config);
 
 /**
  * @brief フォントからコードポイントに対応する文字情報を検索する
  * @param font フォント情報
  * @param code_point 検索するUnicodeコードポイント
  * @return 該当する文字情報のポインタ。見つからない場合はNULL
  */
 const FontCharInfo* epd_text_find_char(const FontInfo* font, uint32_t code_point);
 
 /**
  * @brief UTF-8テキストから次の文字のUnicodeコードポイントを取得する
  * @param text UTF-8テキストへのポインタのポインタ（処理後、ポインタは次の文字位置に進む）
  * @return 次の文字のUnicodeコードポイント。文字列終端の場合は0
  */
 uint32_t epd_text_utf8_next_char(const char** text);
 
 /**
  * @brief 文字が日本語/中国語/韓国語かどうかを判定する
  * @param code_point Unicodeコードポイント
  * @return CJK文字ならtrue、それ以外ならfalse
  */
 bool epd_text_is_cjk(uint32_t code_point);
 
 #endif // EPD_TEXT_H
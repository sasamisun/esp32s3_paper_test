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
  * @brief タイポグラフィフラグの定義
  */
 #define TYPO_FLAG_NEEDS_ROTATION  0x01  // 縦書き時に回転が必要
 #define TYPO_FLAG_HALFWIDTH       0x02  // 半角文字
 #define TYPO_FLAG_FULLWIDTH       0x04  // 全角文字
 #define TYPO_FLAG_NO_BREAK_START  0x08  // 行頭禁則文字
 #define TYPO_FLAG_NO_BREAK_END    0x10  // 行末禁則文字
 
 /**
  * @brief フォント文字情報構造体
  */
 typedef struct {
     uint32_t code_point;   // Unicode値（UTF-32）
     // uint8_t width;         // 文字幅（ピクセル単位）
     uint32_t data_offset;  // ビットマップデータの開始位置
     uint8_t img_width;     // 画像全体の幅
     uint8_t img_height;    // 画像全体の高さ
     uint8_t typo_flags;    // タイポグラフィフラグ
     uint8_t rotation;      // 縦書き時の回転（0: 0度, 1: 90度, 2: 180度, 3: 270度）
     int8_t x_offset;       // X方向オフセット（表示位置の微調整用）
     int8_t y_offset;       // Y方向オフセット（表示位置の微調整用）
 } FontCharInfo;
 
 /**
  * @brief フォント情報
  */
 typedef struct {
     uint8_t size;             // フォントの基本サイズ
     uint8_t max_width;         // 最大の文字幅
     uint8_t max_height;       // 最大の文字高さ
     uint16_t baseline;        // ベースラインの位置（上からの距離）
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
     EPDTextAlignment alignment; // テキスト揃え方
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
     int box_padding;           // 描画領域に対する内側余白
     bool enable_kerning;       // カーニングを有効にするか
     bool use_baseline;         // ベースライン調整を使用するか
     bool use_typo_flags;       // タイポグラフィフラグを使用するか
     bool mono_spacing;         // 等幅かプロポーショナルか

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
 * @param font_char 判定するフォント情報
 * @return 行頭禁止文字の場合はtrue、それ以外はfalse
 */
bool epd_text_is_no_start_char(const FontCharInfo* font_char);

/**
 * @brief 文字が行末禁止文字かどうかを判定する
 * @param font_char 判定するフォント情報
 * @return 行末禁止文字の場合はtrue、それ以外はfalse
 */
bool epd_text_is_no_end_char(const FontCharInfo* font_char);

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
int epd_text_draw_multiline(EPDWrapper* wrapper, EpdRect* rect, const char* text, const EPDTextConfig* config);
 
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
 
 #endif // EPD_TEXT_H
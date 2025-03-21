/**
 * @file epd_text.h
 * @brief 電子ペーパーディスプレイのテキスト描画機能
 * 
 * 電子ペーパーディスプレイ上にテキストを描画するための機能を提供します。
 * 日本語フォント、縦書き、ルビなどに対応しています。
 */

 #ifndef EPD_TEXT_H
 #define EPD_TEXT_H
 
 #include <stdint.h>
 #include <stdbool.h>
 #include "epd_wrapper.h"
 
 #ifdef __cplusplus
 extern "C" {
 #endif

/**
 * @brief フォント文字情報の構造体
 */
typedef struct {
    uint32_t code_point;   // Unicode値（UTF-32に変更）
    uint8_t width;         // 文字幅（ピクセル単位）
    uint32_t data_offset;  // ビットマップデータの開始位置
    uint8_t img_width;     // 画像全体の幅
    uint8_t img_height;    // 画像全体の高さ
} FontCharInfo;

 /**
  * @brief フォント全体の情報を保持する構造体
  */
 typedef struct {
     uint8_t size;             // フォントの基本サイズ
     uint8_t max_height;       // 最大の文字高さ
     const char* style;        // フォントスタイル（regular, bold, italic等）
     uint16_t chars_count;     // 収録文字数
     const FontCharInfo* chars; // 文字情報配列
     const uint8_t* bitmap_data; // ビットマップデータ配列
 } FontInfo;
 
 /**
  * @brief テキストの配置方向の列挙型
  */
 typedef enum {
     EPD_TEXT_ALIGN_LEFT,     // 左揃え（横書き時）/ 上揃え（縦書き時）
     EPD_TEXT_ALIGN_CENTER,   // 中央揃え
     EPD_TEXT_ALIGN_RIGHT     // 右揃え（横書き時）/ 下揃え（縦書き時）
 } EPDTextAlignment;
 
 /**
  * @brief テキスト描画の設定を保持する構造体
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
     bool rotate_non_cjk;       // 非CJK文字（英数字等）を縦書き時に回転するか
     
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
  * @brief テキスト描画設定を初期化（デフォルト値設定）
  * @param config 初期化するテキスト設定構造体へのポインタ
  * @param font 使用するフォント
  */
 void epd_text_config_init(EPDTextConfig* config, const FontInfo* font);
 
 /**
  * @brief 単一文字を描画
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x 描画開始X座標
  * @param y 描画開始Y座標
  * @param code_point 描画する文字のUnicode値
  * @param config テキスト描画設定
  * @return 描画した文字幅
  */
 int epd_text_draw_char(EPDWrapper* wrapper, int x, int y, uint32_t code_point, const EPDTextConfig* config);
 
 /**
  * @brief UTF-8文字列を描画（1行のみ）
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x 描画開始X座標
  * @param y 描画開始Y座標
  * @param text 描画するUTF-8テキスト
  * @param config テキスト描画設定
  * @return 描画したテキスト幅（ピクセル単位）
  */
 int epd_text_draw_string(EPDWrapper* wrapper, int x, int y, const char* text, const EPDTextConfig* config);
 
 /**
  * @brief 複数行テキストを描画（折り返しあり）
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x 描画開始X座標
  * @param y 描画開始Y座標
  * @param text 描画するUTF-8テキスト
  * @param config テキスト描画設定
  * @return 描画した行数
  */
 int epd_text_draw_multiline(EPDWrapper* wrapper, int x, int y, const char* text, const EPDTextConfig* config);
 
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
 int epd_text_draw_ruby(EPDWrapper* wrapper, int x, int y, const char* text, const char* ruby, const EPDTextConfig* config);
 
 /**
  * @brief テキストの描画幅を計算（実際には描画しない）
  * @param text 計算するUTF-8テキスト
  * @param config テキスト描画設定
  * @return テキストの描画幅（ピクセル単位）
  */
 int epd_text_calc_width(const char* text, const EPDTextConfig* config);
 
 /**
  * @brief テキストの描画高さを計算（実際には描画しない）
  * @param text 計算するUTF-8テキスト
  * @param config テキスト描画設定
  * @return テキストの描画高さ（ピクセル単位）
  */
 int epd_text_calc_height(const char* text, const EPDTextConfig* config);
 
 /**
  * @brief フォント内で指定された文字を検索
  * @param font フォント情報
  * @param code_point 検索するUnicode値
  * @return 文字情報へのポインタ（見つからない場合はNULL）
  */
 const FontCharInfo* epd_text_find_char(const FontInfo* font, uint32_t code_point);
 
 /**
  * @brief UTF-8文字列から次の1文字を取得し、ポインタを進める
  * @param text UTF-8文字列へのポインタへのポインタ（ポインタが更新される）
  * @return Unicode値（UTF-32）
  */
 uint32_t epd_text_utf8_next_char(const char** text);
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif /* EPD_TEXT_H */
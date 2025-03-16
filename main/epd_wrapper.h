/**
 * @file epd_wrapper.h
 * @brief 電子ペーパーディスプレイ制御のためのラッパーライブラリ
 * 
 * EPDIYライブラリを使って、ED047TC1ディスプレイを簡単に制御するための
 * ラッパー関数群を提供します。
 */

 #ifndef EPD_WRAPPER_H
 #define EPD_WRAPPER_H
 
 #include <stdint.h>
 #include "epdiy.h"
 #include "epd_highlevel.h"
 
 /**
  * @brief ED047TC1ディスプレイの定数
  */
 #define EPD_DISPLAY_WIDTH 960
 #define EPD_DISPLAY_HEIGHT 540
 #define EPD_DISPLAY_DEPTH 4  // 16 grayscale levels (4 bits)
 
 /**
  * @brief EPDラッパーの状態を保持する構造体
  */
 typedef struct {
     EpdiyHighlevelState hl_state;  // epdiyのハイレベル状態
     uint8_t *framebuffer;          // フレームバッファへのポインタ
     bool is_initialized;           // 初期化済みかどうか
     bool is_powered_on;            // 電源がONかどうか
     int rotation;                  // 画面の回転（0:0度, 1:90度, 2:180度, 3:270度）
 } EPDWrapper;
 
 /**
  * @brief EPDラッパーを初期化する
  * @param wrapper 初期化するEPDラッパー構造体へのポインタ
  * @param board 使用するボード定義
  * @param display 使用するディスプレイ定義
  * @return 初期化に成功したかどうか
  */
 bool epd_wrapper_init(EPDWrapper *wrapper);
 
 /**
  * @brief EPDラッパーを解放する
  * @param wrapper 解放するEPDラッパー構造体へのポインタ
  */
 void epd_wrapper_deinit(EPDWrapper *wrapper);
 
 /**
  * @brief 電源をONにする
  * @param wrapper EPDラッパー構造体へのポインタ
  */
 void epd_wrapper_power_on(EPDWrapper *wrapper);
 
 /**
  * @brief 電源をOFFにする
  * @param wrapper EPDラッパー構造体へのポインタ
  */
 void epd_wrapper_power_off(EPDWrapper *wrapper);
 
 /**
  * @brief ディスプレイを指定した色で塗りつぶす
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param color 指定する色（0x00=黒、0xFF=白）
  */
 void epd_wrapper_fill(EPDWrapper *wrapper, uint8_t color);
 
 /**
  * @brief ディスプレイを複数回のリフレッシュでクリアする
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param cycles クリアサイクル数
  */
 void epd_wrapper_clear_cycles(EPDWrapper *wrapper, int cycles);
 
 /**
  * @brief フレームバッファの内容をディスプレイに反映する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param mode 更新モード（例：MODE_GC16）
  */
 void epd_wrapper_update_screen(EPDWrapper *wrapper, enum EpdDrawMode mode);
 
 /**
  * @brief 円を描画する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x 中心X座標
  * @param y 中心Y座標
  * @param radius 半径
  * @param color 色（0x00=黒、0x0F=薄いグレー、0xF0=濃いグレー、0xFF=白）
  */
 void epd_wrapper_draw_circle(EPDWrapper *wrapper, int x, int y, int radius, uint8_t color);
 
 /**
  * @brief 塗りつぶした円を描画する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x 中心X座標
  * @param y 中心Y座標
  * @param radius 半径
  * @param color 色（0x00=黒、0x0F=薄いグレー、0xF0=濃いグレー、0xFF=白）
  */
 void epd_wrapper_fill_circle(EPDWrapper *wrapper, int x, int y, int radius, uint8_t color);
 
 /**
  * @brief 線を描画する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x0 始点X座標
  * @param y0 始点Y座標
  * @param x1 終点X座標
  * @param y1 終点Y座標
  * @param color 色（0x00=黒、0x0F=薄いグレー、0xF0=濃いグレー、0xFF=白）
  */
 void epd_wrapper_draw_line(EPDWrapper *wrapper, int x0, int y0, int x1, int y1, uint8_t color);
 
 /**
  * @brief 矩形を描画する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x 左上X座標
  * @param y 左上Y座標
  * @param width 幅
  * @param height 高さ
  * @param color 色（0x00=黒、0x0F=薄いグレー、0xF0=濃いグレー、0xFF=白）
  */
 void epd_wrapper_draw_rect(EPDWrapper *wrapper, int x, int y, int width, int height, uint8_t color);
 
 /**
  * @brief 塗りつぶした矩形を描画する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x 左上X座標
  * @param y 左上Y座標
  * @param width 幅
  * @param height 高さ
  * @param color 色（0x00=黒、0x0F=薄いグレー、0xF0=濃いグレー、0xFF=白）
  */
 void epd_wrapper_fill_rect(EPDWrapper *wrapper, int x, int y, int width, int height, uint8_t color);
 
 /**
  * @brief イメージデータを描画する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x 左上X座標
  * @param y 左上Y座標
  * @param width イメージの幅
  * @param height イメージの高さ
  * @param image_data イメージデータ
  */
 void epd_wrapper_draw_image(EPDWrapper *wrapper, int x, int y, int width, int height, const uint8_t *image_data);
 
 /**
  * @brief グレースケールテストパターンを描画する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param x 左上X座標
  * @param y 左上Y座標
  * @param width パターンの幅
  * @param height パターンの高さ
  */
 void epd_wrapper_draw_grayscale_test(EPDWrapper *wrapper, int x, int y, int width, int height);
 
 /**
  * @brief フレームバッファへの直接アクセスを提供する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @return フレームバッファへのポインタ
  */
 uint8_t* epd_wrapper_get_framebuffer(EPDWrapper *wrapper);
 
 /**
  * @brief ディスプレイの回転を設定する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param rotation 回転値（0: 0度, 1: 90度, 2: 180度, 3: 270度）
  * @return 設定に成功したかどうか
  */
 bool epd_wrapper_set_rotation(EPDWrapper *wrapper, int rotation);
 
 /**
  * @brief 現在のディスプレイ回転を取得する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @return 現在の回転値（0: 0度, 1: 90度, 2: 180度, 3: 270度）、エラー時は-1
  */
 int epd_wrapper_get_rotation(EPDWrapper *wrapper);
 
 /**
  * @brief 回転を考慮したディスプレイの幅を取得する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @return 幅
  */
 int epd_wrapper_get_width(EPDWrapper *wrapper);
 
 /**
  * @brief 回転を考慮したディスプレイの高さを取得する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @return 高さ
  */
 int epd_wrapper_get_height(EPDWrapper *wrapper);
 
 #endif // EPD_WRAPPER_H
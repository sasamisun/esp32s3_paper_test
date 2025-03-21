/**
 * @file epd_transition.h
 * @brief 電子ペーパーディスプレイのトランジションエフェクト機能
 */

 #ifndef EPD_TRANSITION_H
 #define EPD_TRANSITION_H
 
 #include "epd_wrapper.h"
 
 /**
  * @brief トランジションの種類
  */
 typedef enum {
     TRANSITION_FADE,           // フェードイン/アウト
     TRANSITION_SLIDE_LEFT,     // 左からスライド
     TRANSITION_SLIDE_RIGHT,    // 右からスライド
     TRANSITION_SLIDE_UP,       // 上からスライド
     TRANSITION_SLIDE_DOWN,     // 下からスライド
     TRANSITION_WIPE,           // ワイプ効果
     TRANSITION_CUSTOM          // カスタムマスク使用
 } TransitionType;
 
 /**
  * @brief トランジション情報を保持する構造体
  */
 typedef struct {
     uint8_t *framebuffer_next;     // 次のフレームバッファ (PSRAMに配置)
     uint8_t *transition_mask;      // トランジションマスク画像
     int transition_width;          // トランジションマスクの幅
     int transition_height;         // トランジションマスクの高さ
     TransitionType type;           // トランジションの種類
     int steps;                     // トランジションのステップ数 (2,4,8,16)
     int current_step;              // 現在のステップ
     bool is_active;                // トランジションが進行中か
     enum EpdDrawMode update_mode;  // 画面更新モード
 } EPDTransition;
 
 /**
  * @brief トランジション機能を初期化する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param transition 初期化するトランジション構造体へのポインタ
  * @param steps トランジションのステップ数 (2,4,8,16のいずれか)
  * @return 初期化に成功したかどうか
  */
 bool epd_transition_init(EPDWrapper *wrapper, EPDTransition *transition, int steps);
 
 /**
  * @brief トランジションの準備を行う
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param transition トランジション構造体へのポインタ
  * @param type トランジションの種類
  * @param update_mode 画面更新モード
  * @return 準備に成功したかどうか
  */
 bool epd_transition_prepare(EPDWrapper *wrapper, EPDTransition *transition, 
                            TransitionType type, enum EpdDrawMode update_mode);
 
 /**
  * @brief カスタムマスク画像でのトランジション準備
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param transition トランジション構造体へのポインタ
  * @param mask_data マスク画像データ
  * @param width マスク画像の幅
  * @param height マスク画像の高さ
  * @param update_mode 画面更新モード
  * @return 準備に成功したかどうか
  */
 bool epd_transition_prepare_with_mask(EPDWrapper *wrapper, EPDTransition *transition,
                                      const uint8_t *mask_data, int width, int height,
                                      enum EpdDrawMode update_mode);
 
 /**
  * @brief 次画面のフレームバッファを取得する
  * @param transition トランジション構造体へのポインタ
  * @return 次画面のフレームバッファへのポインタ
  */
 uint8_t *epd_transition_get_next_framebuffer(EPDTransition *transition);
 
 /**
  * @brief トランジションのステップを実行する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param transition トランジション構造体へのポインタ
  * @return 成功したかどうか
  */
 bool epd_transition_step(EPDWrapper *wrapper, EPDTransition *transition);
 
 /**
  * @brief トランジションを完了する (一度に残りのステップを実行)
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param transition トランジション構造体へのポインタ
  * @return 成功したかどうか
  */
 bool epd_transition_complete(EPDWrapper *wrapper, EPDTransition *transition);
 
 /**
  * @brief トランジションリソースを解放する
  * @param wrapper EPDラッパー構造体へのポインタ
  * @param transition トランジション構造体へのポインタ
  */
 void epd_transition_deinit(EPDWrapper *wrapper, EPDTransition *transition);
 
 #endif // EPD_TRANSITION_H
typedef struct {
    // 画面レイヤー管理
    uint8_t *bg_layer;       // 背景画像レイヤー用バッファ
    uint8_t *fg_layer;       // 前景画像（キャラクター、ボタン）レイヤー用バッファ
    uint8_t *text_layer;     // テキストレイヤー用バッファ
    uint8_t *composite_buf;  // 合成用バッファ
    
    // 画面パラメータ
    uint8_t rotation;        // 画面の回転状態 (0, 1, 2, 3)
    uint16_t width;          // 現在の画面幅
    uint16_t height;         // 現在の画面高さ
    
    // EPDラッパー
    EPDWrapper *epd;         // EPDラッパーへのポインタ
    EPDTransition *transition; // 画面遷移エフェクト用
    
    // テキスト描画設定
    EPDTextConfig text_config; // テキスト描画設定
    const FontInfo *font;      // 現在使用中のフォント
    
    // タッチ制御
    GT911_Device *touch;     // タッチコントローラ
    bool touch_enabled;      // タッチ機能の有効状態
    TouchArea *touch_areas;  // タッチ検出エリア配列
    uint8_t touch_areas_count; // タッチエリア数
    
    // ストレージ管理
    bool sd_mounted;         // SDカードマウント状態
    bool usb_msc_active;     // USB MSC機能の状態
    
    // ゲーム状態管理
    GameState current_state; // 現在のゲーム状態
    SceneData *scenes;       // シーンデータ配列
    uint16_t scene_count;    // シーン数
    uint16_t current_scene;  // 現在のシーン番号
    
    // リソース管理
    ResourceCache *resources; // 画像・音声リソースキャッシュ
    
    // イベント処理
    EventHandler *event_handlers; // イベントハンドラ配列
    
    // タイマー管理
    uint32_t last_update_time; // 最後の更新時間
    
    // 設定情報
    AyameConfig config;      // エンジン設定
    
} AyameEngine;

// フォントリスト
typedef enum {
    FONT_SMALL,
    FONT_MIDIUM,
    FONT_LARGE,
    FONT_CUSTOM
} Fonts;

// ゲームの状態
typedef enum {
    GAME_UNLOADED,
    GAME_LOADED,
    GAME_PLAYING,
    GAME_SPECIAL
} GameState;
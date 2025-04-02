/**
 * @file gt911.h
 * @brief GT911タッチコントローラのインターフェース定義
 */

#ifndef GT911_H
#define GT911_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

// I2Cの設定
#define GT911_I2C_PORT I2C_NUM_0
#define GT911_I2C_SDA_PIN 41 // SDAピン
#define GT911_I2C_SCL_PIN 42 // SCLピン
#define GT911_INT_PIN 48     // 割り込みピン

#define GT911_I2C_ADDR_DEFAULT 0x5D // 0xBA/2、7ビットアドレス形式
#define GT911_I2C_ADDR_ALT 0x14     // 0x28/2、代替アドレス

// I2C通信パラメータ
#define GT911_I2C_TIMEOUT_MS 100 // I2C通信のタイムアウト時間
#define GT911_I2C_FREQ_HZ 400000 // I2C通信周波数 (400kHz)

// リセットタイミング
#define GT911_RESET_LOW_MS 20   // リセット時のLOW保持時間
#define GT911_INT_LOW_MS 50     // INT信号LOW保持時間
#define GT911_RESET_HIGH_MS 100 // リセット解除後の待機時間

// コマンドレジスタ
#define GT911_REG_COMMAND 0x8040       // コマンドレジスタ
#define GT911_REG_ESD_CHECK 0x8041     // ESDチェックレジスタ
#define GT911_REG_COMMAND_CHECK 0x8046 // コマンド確認レジスタ

// GT911コマンド
#define GT911_CMD_READ_COORD 0x00 // 座標読取モード
#define GT911_CMD_SCREEN_OFF 0x05 // 画面オフ/スリープモード
#define GT911_CMD_SCREEN_ON 0x06  // スリープモードから復帰
#define GT911_CMD_GESTURE_ON 0x08 // ジェスチャーモード有効
#define GT911_CMD_ESD_CHECK 0xAA  // ESD保護メカニズム

// 設定情報
#define GT911_REG_CONFIG_DATA 0x8047    // 設定データの開始アドレス
#define GT911_REG_CONFIG_VERSION 0x8047 // 設定バージョン
#define GT911_REG_X_RESOLUTION 0x8048   // X解像度
#define GT911_REG_Y_RESOLUTION 0x804A   // Y解像度
#define GT911_REG_TOUCH_NUMBER 0x804C   // タッチポイント数

// モジュールスイッチレジスタ
#define GT911_REG_MODULE_SWITCH1 0x804D // モジュールスイッチ1
#define GT911_REG_MODULE_SWITCH2 0x804E // モジュールスイッチ2

// タッチ感度設定
#define GT911_REG_TOUCH_LEVEL 0x8053  // タッチ検出しきい値
#define GT911_REG_LEAVE_LEVEL 0x8054  // タッチリリースしきい値
#define GT911_REG_REFRESH_RATE 0x8056 // リフレッシュレート

// タッチキー設定
#define GT911_REG_KEY1_ADDR 0x8093    // キー1のアドレス
#define GT911_REG_KEY2_ADDR 0x8094    // キー2のアドレス
#define GT911_REG_KEY3_ADDR 0x8095    // キー3のアドレス
#define GT911_REG_KEY4_ADDR 0x8096    // キー4のアドレス
#define GT911_REG_KEY_AREA 0x8097     // キー領域設定
#define GT911_REG_KEY_TOUCH 0x8098    // キータッチしきい値
#define GT911_REG_KEY_LEAVE 0x8099    // キーリリースしきい値
#define GT911_REG_KEY_SENS1 0x809A    // キー1と2の感度係数
#define GT911_REG_KEY_SENS2 0x809B    // キー3と4の感度係数
#define GT911_REG_KEY_RESTRAIN 0x809C // キー抑制パラメータ

// 設定更新とチェックサム
#define GT911_REG_CONFIG_CHECKSUM 0x80FF // 設定チェックサム
#define GT911_REG_CONFIG_FRESH 0x8100    // 設定更新フラグ

// 製品ID
#define GT911_REG_PRODUCT_ID 0x8140 // 製品ID (4バイト)

// ステータスと座標
#define GT911_REG_STATUS 0x814E // ステータスレジスタ
#define GT911_REG_TOUCH1 0x8150 // 最初のタッチポイントデータ
#define GT911_REG_POINT_SIZE 8  // 各タッチポイントのデータサイズ

// ステータスレジスタビット
#define GT911_STATUS_TOUCH 0x80      // タッチ有りフラグ
#define GT911_STATUS_TOUCH_MASK 0x0F // タッチ数マスク

// タッチポイント最大数
#define GT911_MAX_TOUCH_POINTS 2 // 最大2点までのマルチタッチをサポート

// モジュールスイッチビット
#define GT911_SWITCH_Y_REVERSE 0x80 // Y軸反転 (bit7)
#define GT911_SWITCH_X_REVERSE 0x40 // X軸反転 (bit6)
#define GT911_SWITCH_XY_SWAP 0x08   // X/Y軸交換 (bit3)
#define GT911_SWITCH_NOISE_RED 0x04 // ノイズ低減 (bit2)
#define GT911_INT_TRIGGER_MASK 0x03 // INT起動モードマスク (bit0-1)

// モジュールスイッチ2ビット
#define GT911_SWITCH_TOUCH_KEY 0x01 // タッチキー有効 (bit0)

// タッチポイント構造体
typedef struct
{
    uint16_t x;          // X座標
    uint16_t y;          // Y座標
    uint16_t size;       // タッチサイズ/強度
    uint8_t tracking_id; // 追跡ID
    bool is_pressed;     // 押下状態
} GT911_TouchPoint;

// タッチキー構造体
typedef struct
{
    uint8_t key_id;      // キーID (1-4)
    uint8_t address;     // キーアドレス設定
    uint8_t sensitivity; // 感度係数 (0-15)
    bool is_pressed;     // 押下状態
} GT911_TouchKey;

// 前方宣言
typedef struct GT911_Device GT911_Device1;

// タッチイベント通知用コールバック関数の型定義
typedef void (*gt911_touch_callback_t)(GT911_Device1* device, void* user_data);

// GT911状態管理構造体
typedef struct
{
    bool is_initialized;                             // 初期化済みフラグ
    i2c_port_t i2c_port;                             // 使用するI2Cポート
    uint8_t i2c_addr;                                // I2Cアドレス（0xBAまたは0x28）
    uint16_t x_resolution;                           // X解像度
    uint16_t y_resolution;                           // Y解像度
    bool x_reverse;                                  // X軸反転フラグ
    bool y_reverse;                                  // Y軸反転フラグ
    bool xy_swap;                                    // XY軸入れ替えフラグ
    uint8_t active_points;                           // アクティブなタッチポイント数
    GT911_TouchPoint points[GT911_MAX_TOUCH_POINTS]; // タッチポイントデータ

    // タッチキー関連
    bool touch_key_enabled; // タッチキー有効フラグ
    uint8_t active_keys;    // アクティブなキー数
    GT911_TouchKey keys[4]; // タッチキーデータ (最大4つ)

    // GPIO設定
    gpio_num_t int_pin; // 割り込みピン
    gpio_num_t rst_pin; // リセットピン（使用する場合）

    // 割り込み処理用
    SemaphoreHandle_t touch_semaphore; // タッチイベント通知用セマフォ
    TaskHandle_t interrupt_task;       // 割り込み処理タスク
    //gt911_touch_callback_t callback;   // タッチコールバック関数
    void *user_data;                   // ユーザーデータポインタ
} GT911_Device;


/**
 * @brief GT911を初期化する
 * @param device GT911デバイス構造体へのポインタ
 * @param sda_pin SDAピン番号
 * @param scl_pin SCLピン番号
 * @param int_pin INTピン番号
 * @param rst_pin RSTピン番号（不要な場合は-1を指定）
 * @return 初期化成功の場合true、失敗の場合false
 */
bool gt911_init(GT911_Device *device, gpio_num_t sda_pin, gpio_num_t scl_pin,
                gpio_num_t int_pin, gpio_num_t rst_pin);

/**
 * @brief GT911を終了し、リソースを解放する
 * @param device GT911デバイス構造体へのポインタ
 */
void gt911_deinit(GT911_Device *device);

/**
 * @brief GT911のレジスタから特定のバイト数を読み取る
 * @param device GT911デバイス構造体へのポインタ
 * @param reg 読み取り開始レジスタアドレス
 * @param data 読み取ったデータを格納するバッファ
 * @param len 読み取るバイト数
 * @return 成功の場合true、失敗の場合false
 */
bool gt911_read_registers(GT911_Device *device, uint16_t reg, uint8_t *data, size_t len);

/**
 * @brief GT911のレジスタに特定のバイト数を書き込む
 * @param device GT911デバイス構造体へのポインタ
 * @param reg 書き込み開始レジスタアドレス
 * @param data 書き込むデータバッファ
 * @param len 書き込むバイト数
 * @return 成功の場合true、失敗の場合false
 */
bool gt911_write_registers(GT911_Device *device, uint16_t reg, const uint8_t *data, size_t len);

/**
 * @brief タッチデータを読み取る
 * @param device GT911デバイス構造体へのポインタ
 * @return 読み取り成功の場合true、失敗の場合false
 */
bool gt911_read_touch_data(GT911_Device *device);

/**
 * @brief タッチキーの状態を読み取る
 * @param device GT911デバイス構造体へのポインタ
 * @return 読み取り成功の場合true、失敗の場合false
 */
bool gt911_read_touch_keys(GT911_Device *device);

/**
 * @brief コールバック関数を登録する
 * @param device GT911デバイス構造体へのポインタ
 * @param callback タッチイベント発生時に呼び出されるコールバック関数
 * @param user_data コールバック関数に渡されるユーザーデータ
 */
void gt911_register_callback(GT911_Device *device, gt911_touch_callback_t callback, void *user_data);

/**
 * @brief GT911をスリープモードに切り替える
 * @param device GT911デバイス構造体へのポインタ
 * @return 成功の場合true、失敗の場合false
 */
bool gt911_enter_sleep_mode(GT911_Device *device);

/**
 * @brief GT911をスリープモードから復帰させる
 * @param device GT911デバイス構造体へのポインタ
 * @return 成功の場合true、失敗の場合false
 */
bool gt911_exit_sleep_mode(GT911_Device *device);

/**
 * @brief GT911の設定を更新する
 * @param device GT911デバイス構造体へのポインタ
 * @param config 設定データバッファ（少なくとも186バイト）
 * @return 成功の場合true、失敗の場合false
 */
bool gt911_update_config(GT911_Device *device, const uint8_t *config);

/**
 * @brief GT911のハードウェアリセットを実行する
 * @param device GT911デバイス構造体へのポインタ
 */
void gt911_reset(GT911_Device *device);

/**
 * @brief GT911のステータスをクリアする
 * @param device GT911デバイス構造体へのポインタ
 * @return 成功の場合true、失敗の場合false
 */
bool gt911_clear_status(GT911_Device *device);

/**
 * @brief GT911の製品IDを取得する
 * @param device GT911デバイス構造体へのポインタ
 * @param product_id 製品IDを格納するバッファ（少なくとも4バイト）
 * @return 成功の場合true、失敗の場合false
 */
bool gt911_get_product_id(GT911_Device *device, uint8_t *product_id);

/**
 * @brief タッチ感度を設定する
 * @param device GT911デバイス構造体へのポインタ
 * @param touch_threshold タッチ検出しきい値 (0-255)
 * @param leave_threshold タッチリリースしきい値 (0-255)
 * @return 成功の場合true、失敗の場合false
 */
bool gt911_set_sensitivity(GT911_Device *device, uint8_t touch_threshold, uint8_t leave_threshold);

/**
 * @brief タッチキーを設定する
 * @param device GT911デバイス構造体へのポインタ
 * @param enable キー機能を有効にするかどうか
 * @return 成功の場合true、失敗の場合false
 */
bool gt911_configure_touch_keys(GT911_Device *device, bool enable);

/**
 * @brief 特定のタッチキーを設定する
 * @param device GT911デバイス構造体へのポインタ
 * @param key_id キーID (1-4)
 * @param address キーアドレス (0-255, 0は無効)
 * @param sensitivity キーの感度 (0-15)
 * @return 成功の場合true、失敗の場合false
 */
bool gt911_set_touch_key(GT911_Device *device, uint8_t key_id, uint8_t address, uint8_t sensitivity);

/**
 * @brief リフレッシュレートを設定する
 * @param device GT911デバイス構造体へのポインタ
 * @param rate リフレッシュレート (5+N ms)
 * @return 成功の場合true、失敗の場合false
 */
bool gt911_set_refresh_rate(GT911_Device *device, uint8_t rate);

#endif // GT911_H
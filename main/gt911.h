/**
 * @file gt911.h
 * @brief GT911タッチコントローラのインターフェース定義
 */

 #ifndef GT911_H
 #define GT911_H
 
 #include <stdint.h>
 #include <stdbool.h>
 #include "driver/i2c.h"
 
 // I2Cの設定
 #define GT911_I2C_PORT          I2C_NUM_0
 #define GT911_I2C_ADDR_DEFAULT  0x5D  // 0xBA/2、7ビットアドレス形式
 #define GT911_I2C_ADDR_ALT      0x28/0x29  // 代替アドレス
 
 // 重要なレジスタアドレス
 #define GT911_REG_STATUS        0x8140  // ステータスレジスタ
 #define GT911_REG_TOUCH1        0x8150  // 最初のタッチポイントデータ
 #define GT911_REG_POINT_SIZE    8       // 各タッチポイントのデータサイズ
 
 // タッチポイント最大数
 #define GT911_MAX_TOUCH_POINTS  5
 
 // タッチポイント構造体
 typedef struct {
     uint16_t x;          // X座標
     uint16_t y;          // Y座標
     uint16_t size;       // タッチサイズ/強度
     uint8_t tracking_id; // 追跡ID
     bool is_pressed;     // 押下状態
 } GT911_TouchPoint;
 
 // GT911状態管理構造体
 typedef struct {
     bool is_initialized;             // 初期化済みフラグ
     i2c_port_t i2c_port;             // 使用するI2Cポート
     uint8_t i2c_addr;                // I2Cアドレス（0xBAまたは0x28）
     uint8_t active_points;           // アクティブなタッチポイント数
     GT911_TouchPoint points[GT911_MAX_TOUCH_POINTS]; // タッチポイントデータ
     gpio_num_t int_pin;              // 割り込みピン
     gpio_num_t rst_pin;              // リセットピン（使用する場合）
 } GT911_Device;
 
 // タッチイベント通知用コールバック関数の型定義
 typedef void (*gt911_touch_callback_t)(GT911_Device* device);
 
 /**
  * @brief GT911を初期化する
  * @param device GT911デバイス構造体へのポインタ
  * @param sda_pin SDAピン番号
  * @param scl_pin SCLピン番号
  * @param int_pin INTピン番号
  * @param rst_pin RSTピン番号（不要な場合は-1を指定）
  * @return 初期化成功の場合true、失敗の場合false
  */
 bool gt911_init(GT911_Device* device, gpio_num_t sda_pin, gpio_num_t scl_pin, 
                 gpio_num_t int_pin, gpio_num_t rst_pin);
 
 /**
  * @brief GT911を終了し、リソースを解放する
  * @param device GT911デバイス構造体へのポインタ
  */
 void gt911_deinit(GT911_Device* device);
 
 /**
  * @brief タッチデータを読み取る
  * @param device GT911デバイス構造体へのポインタ
  * @return 読み取り成功の場合true、失敗の場合false
  */
 bool gt911_read_touch_data(GT911_Device* device);
 
 /**
  * @brief コールバック関数を登録する
  * @param device GT911デバイス構造体へのポインタ
  * @param callback タッチイベント発生時に呼び出されるコールバック関数
  */
 void gt911_register_callback(GT911_Device* device, gt911_touch_callback_t callback);
 
 /**
  * @brief GT911をスリープモードに切り替える
  * @param device GT911デバイス構造体へのポインタ
  * @return 成功の場合true、失敗の場合false
  */
 bool gt911_enter_sleep_mode(GT911_Device* device);
 
 /**
  * @brief GT911をスリープモードから復帰させる
  * @param device GT911デバイス構造体へのポインタ
  * @return 成功の場合true、失敗の場合false
  */
 bool gt911_exit_sleep_mode(GT911_Device* device);
 
 #endif // GT911_H
/**
 * @file gt911.c
 * @brief GT911タッチコントローラドライバの実装
 */

 #include <string.h>
 #include "gt911.h"
 #include "esp_log.h"
 #include "driver/gpio.h"
 #include "freertos/task.h"
 
 static const char* TAG = "GT911";
 
 // GT911初期設定データ
 static uint8_t gt911_default_config[] = {
     // 0x8047 - 0x8053: 基本設定
     0x00, // 設定バージョン
     0xC0, 0x03, // X解像度 (960)
     0x1C, 0x02, // Y解像度 (540)
     0x02, // タッチポイント数 (2)
     0x00, // モジュールスイッチ1
     0x01, // モジュールスイッチ2 (タッチキー有効)
     0x05, // Shake Count
     0x50, // フィルタ
     // 0x8053 - 0x805A: タッチとリフレッシュ設定
     0x32, // タッチしきい値 (50)
     0x28, // リリースしきい値 (40)
     0x0F, // 低電力制御
     0x05, // リフレッシュレート (10ms)
     0x00, // X座標しきい値
     0x00, // Y座標しきい値
     0x00, // X速度制限
     0x00, // Y速度制限
     // 残りは省略（実際の用途に合わせて適宜追加）
 };
 
 // プロトタイプ宣言
 static void IRAM_ATTR gt911_interrupt_handler(void* arg);
 static void gt911_task(void* pvParameters);
 static bool gt911_calculate_checksum(uint8_t* config, size_t len);
 
 /**
  * @brief GT911を初期化する
  */
 bool gt911_init(GT911_Device* device, gpio_num_t sda_pin, gpio_num_t scl_pin, 
                 gpio_num_t int_pin, gpio_num_t rst_pin) {
     if (device == NULL) {
         ESP_LOGE(TAG, "Device pointer is NULL");
         return false;
     }
 
     // デバイスの初期化
     memset(device, 0, sizeof(GT911_Device));
     device->i2c_port = GT911_I2C_PORT;
     device->i2c_addr = GT911_I2C_ADDR_DEFAULT;
     device->int_pin = int_pin;
     device->rst_pin = rst_pin;
     
     // 解像度設定
     device->x_resolution = 960;
     device->y_resolution = 540;
 
     // I2C設定
     i2c_config_t i2c_conf = {
         .mode = I2C_MODE_MASTER,
         .sda_io_num = sda_pin,
         .scl_io_num = scl_pin,
         .sda_pullup_en = GPIO_PULLUP_ENABLE,
         .scl_pullup_en = GPIO_PULLUP_ENABLE,
         .master.clk_speed = GT911_I2C_FREQ_HZ
     };
 
     esp_err_t ret = i2c_param_config(device->i2c_port, &i2c_conf);
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "I2C parameter config failed: %s", esp_err_to_name(ret));
         return false;
     }
 
     ret = i2c_driver_install(device->i2c_port, I2C_MODE_MASTER, 0, 0, 0);
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
         return false;
     }
     
     // GPIOの設定
     if (int_pin != GPIO_NUM_NC) {
         gpio_config_t io_conf = {
             .pin_bit_mask = 1ULL << int_pin,
             .mode = GPIO_MODE_INPUT,
             .pull_up_en = GPIO_PULLUP_ENABLE,
             .intr_type = GPIO_INTR_DISABLE
         };
         gpio_config(&io_conf);
     }
     
     if (rst_pin != GPIO_NUM_NC) {
         gpio_config_t io_conf = {
             .pin_bit_mask = 1ULL << rst_pin,
             .mode = GPIO_MODE_OUTPUT,
             .pull_up_en = GPIO_PULLUP_DISABLE,
             .intr_type = GPIO_INTR_DISABLE
         };
         gpio_config(&io_conf);
     }
     
     // チップのリセット
     gt911_reset(device);
     
     // 製品IDを確認
     uint8_t product_id[4] = {0};
     if (!gt911_get_product_id(device, product_id)) {
         ESP_LOGE(TAG, "Failed to get product ID");
         gt911_deinit(device);
         return false;
     }
     
     ESP_LOGI(TAG, "GT911 Product ID: %02X%02X%02X%02X", 
              product_id[0], product_id[1], product_id[2], product_id[3]);
     
     // 設定データを更新
     // 解像度などの基本設定を反映
     uint8_t config[sizeof(gt911_default_config)];
     memcpy(config, gt911_default_config, sizeof(gt911_default_config));
     
     // X解像度
     config[1] = device->x_resolution & 0xFF;
     config[2] = (device->x_resolution >> 8) & 0xFF;
     
     // Y解像度
     config[3] = device->y_resolution & 0xFF;
     config[4] = (device->y_resolution >> 8) & 0xFF;
     
     // タッチポイント数
     config[5] = GT911_MAX_TOUCH_POINTS;
     
     // チェックサム計算と設定送信
     if (!gt911_update_config(device, config)) {
         ESP_LOGE(TAG, "Failed to update config");
         gt911_deinit(device);
         return false;
     }
     
     // 割り込み設定
     device->touch_semaphore = xSemaphoreCreateBinary();
     if (device->touch_semaphore == NULL) {
         ESP_LOGE(TAG, "Failed to create semaphore");
         gt911_deinit(device);
         return false;
     }
     
     // 割り込みハンドラとタスクを作成
     gpio_set_intr_type(int_pin, GPIO_INTR_NEGEDGE);
     gpio_install_isr_service(0);
     gpio_isr_handler_add(int_pin, gt911_interrupt_handler, device);
     
     // タスク作成
     xTaskCreate(gt911_task, "gt911_task", 2048, device, 10, &device->interrupt_task);
     
     ESP_LOGI(TAG, "GT911 initialized successfully");
     device->is_initialized = true;
     
     return true;
 }
 
 /**
  * @brief GT911を終了し、リソースを解放する
  */
 void gt911_deinit(GT911_Device* device) {
     if (device == NULL || !device->is_initialized) {
         return;
     }
     
     // 割り込みハンドラを解除
     if (device->int_pin != GPIO_NUM_NC) {
         gpio_isr_handler_remove(device->int_pin);
         gpio_set_intr_type(device->int_pin, GPIO_INTR_DISABLE);
     }
     
     // タスクを終了
     if (device->interrupt_task != NULL) {
         vTaskDelete(device->interrupt_task);
         device->interrupt_task = NULL;
     }
     
     // セマフォを解放
     if (device->touch_semaphore != NULL) {
         vSemaphoreDelete(device->touch_semaphore);
         device->touch_semaphore = NULL;
     }
     
     // I2Cドライバを解放
     i2c_driver_delete(device->i2c_port);
     
     device->is_initialized = false;
     ESP_LOGI(TAG, "GT911 deinitialized");
 }
 
 /**
  * @brief GT911のレジスタから特定のバイト数を読み取る
  */
 bool gt911_read_registers(GT911_Device* device, uint16_t reg, uint8_t* data, size_t len) {
     if (device == NULL || !device->is_initialized || data == NULL) {
         return false;
     }
     
     // レジスタアドレスを設定
     uint8_t buf[2];
     buf[0] = (reg >> 8) & 0xFF;
     buf[1] = reg & 0xFF;
     
     i2c_cmd_handle_t cmd = i2c_cmd_link_create();
     esp_err_t ret;
     
     // レジスタアドレス書き込み
     i2c_master_start(cmd);
     i2c_master_write_byte(cmd, (device->i2c_addr << 1) | I2C_MASTER_WRITE, true);
     i2c_master_write(cmd, buf, 2, true);
     i2c_master_stop(cmd);
     
     ret = i2c_master_cmd_begin(device->i2c_port, cmd, GT911_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
     i2c_cmd_link_delete(cmd);
     
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "I2C write reg address failed: %s", esp_err_to_name(ret));
         return false;
     }
     
     // データ読み取り
     cmd = i2c_cmd_link_create();
     i2c_master_start(cmd);
     i2c_master_write_byte(cmd, (device->i2c_addr << 1) | I2C_MASTER_READ, true);
     i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK);
     i2c_master_stop(cmd);
     
     ret = i2c_master_cmd_begin(device->i2c_port, cmd, GT911_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
     i2c_cmd_link_delete(cmd);
     
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "I2C read data failed: %s", esp_err_to_name(ret));
         return false;
     }
     
     return true;
 }
 
 /**
  * @brief GT911のレジスタに特定のバイト数を書き込む
  */
 bool gt911_write_registers(GT911_Device* device, uint16_t reg, const uint8_t* data, size_t len) {
     if (device == NULL || !device->is_initialized || data == NULL) {
         return false;
     }
     
     uint8_t buf[2];
     buf[0] = (reg >> 8) & 0xFF;
     buf[1] = reg & 0xFF;
     
     i2c_cmd_handle_t cmd = i2c_cmd_link_create();
     esp_err_t ret;
     
     i2c_master_start(cmd);
     i2c_master_write_byte(cmd, (device->i2c_addr << 1) | I2C_MASTER_WRITE, true);
     i2c_master_write(cmd, buf, 2, true);
     i2c_master_write(cmd, data, len, true);
     i2c_master_stop(cmd);
     
     ret = i2c_master_cmd_begin(device->i2c_port, cmd, GT911_I2C_TIMEOUT_MS / portTICK_PERIOD_MS);
     i2c_cmd_link_delete(cmd);
     
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "I2C write data failed: %s", esp_err_to_name(ret));
         return false;
     }
     
     return true;
 }
 
 /**
  * @brief タッチデータを読み取る
  */
 bool gt911_read_touch_data(GT911_Device* device) {
     if (device == NULL || !device->is_initialized) {
         return false;
     }
     
     // ステータスレジスタを読み取り
     uint8_t status;
     if (!gt911_read_registers(device, GT911_REG_STATUS, &status, 1)) {
         ESP_LOGE(TAG, "Failed to read status register");
         return false;
     }
     
     // タッチイベントチェック
     if (status & GT911_STATUS_TOUCH) {
         // タッチポイント数を取得
         device->active_points = status & GT911_STATUS_TOUCH_MASK;
         if (device->active_points > GT911_MAX_TOUCH_POINTS) {
             device->active_points = GT911_MAX_TOUCH_POINTS;
         }
         
         // 各タッチポイントのデータを読み取り
         if (device->active_points > 0) {
             for (uint8_t i = 0; i < device->active_points; i++) {
                 uint8_t point_data[GT911_REG_POINT_SIZE];
                 uint16_t point_addr = GT911_REG_TOUCH1 + (i * GT911_REG_POINT_SIZE);
                 
                 if (!gt911_read_registers(device, point_addr, point_data, GT911_REG_POINT_SIZE)) {
                     ESP_LOGE(TAG, "Failed to read touch point %d data", i);
                     continue;
                 }
                 
                 // X座標 (2バイト, リトルエンディアン)
                 device->points[i].x = point_data[0] | (point_data[1] << 8);
                 
                 // Y座標 (2バイト, リトルエンディアン)
                 device->points[i].y = point_data[2] | (point_data[3] << 8);
                 
                 // サイズ (2バイト, リトルエンディアン)
                 device->points[i].size = point_data[4] | (point_data[5] << 8);
                 
                 // 追跡ID
                 device->points[i].tracking_id = point_data[6];
                 
                 // 押下状態
                 device->points[i].is_pressed = true;
                 
                 ESP_LOGD(TAG, "Touch point %d: x=%d, y=%d, size=%d, id=%d",
                         i, device->points[i].x, device->points[i].y, 
                         device->points[i].size, device->points[i].tracking_id);
             }
         }
         
         // ステータスレジスタをクリア
         if (!gt911_clear_status(device)) {
             ESP_LOGE(TAG, "Failed to clear status register");
         }
         
         return true;
     } else {
         // タッチなしの場合
         device->active_points = 0;
         for (uint8_t i = 0; i < GT911_MAX_TOUCH_POINTS; i++) {
             device->points[i].is_pressed = false;
         }
     }
     
     return false;
 }
 
 /**
  * @brief タッチキーの状態を読み取る (スタブ実装)
  */
 bool gt911_read_touch_keys(GT911_Device* device) {
     // スタブ実装
     ESP_LOGW(TAG, "gt911_read_touch_keys not implemented");
     return false;
 }
 
 /**
  * @brief コールバック関数を登録する
  */
 void gt911_register_callback(GT911_Device* device, gt911_touch_callback_t callback, void* user_data) {
     if (device == NULL || !device->is_initialized) {
         return;
     }
     
     device->callback = callback;
     device->user_data = user_data;
 }
 
 /**
  * @brief GT911をスリープモードに切り替える (スタブ実装)
  */
 bool gt911_enter_sleep_mode(GT911_Device* device) {
     // スタブ実装
     ESP_LOGW(TAG, "gt911_enter_sleep_mode not implemented");
     return false;
 }
 
 /**
  * @brief GT911をスリープモードから復帰させる (スタブ実装)
  */
 bool gt911_exit_sleep_mode(GT911_Device* device) {
     // スタブ実装
     ESP_LOGW(TAG, "gt911_exit_sleep_mode not implemented");
     return false;
 }
 
 /**
  * @brief GT911の設定を更新する
  */
 bool gt911_update_config(GT911_Device* device, const uint8_t* config) {
     if (device == NULL || !device->is_initialized || config == NULL) {
         return false;
     }
     
     // まず設定のチェックサムを計算
     uint8_t config_with_checksum[sizeof(gt911_default_config) + 2]; // +2 for checksum and update flag
     memcpy(config_with_checksum, config, sizeof(gt911_default_config));
     
     // チェックサムを計算して設定
     if (!gt911_calculate_checksum(config_with_checksum, sizeof(gt911_default_config))) {
         ESP_LOGE(TAG, "Failed to calculate checksum");
         return false;
     }
     
     // 設定更新フラグを設定
     config_with_checksum[sizeof(gt911_default_config) + 1] = 0x01;
     
     // 設定をレジスタに書き込み
     if (!gt911_write_registers(device, GT911_REG_CONFIG_DATA, config_with_checksum, 
                               sizeof(gt911_default_config) + 2)) {
         ESP_LOGE(TAG, "Failed to write config data");
         return false;
     }
     
     // 設定が適用されるまで少し待機
     vTaskDelay(100 / portTICK_PERIOD_MS);
     
     return true;
 }
 
 /**
  * @brief GT911のハードウェアリセットを実行する
  */
 void gt911_reset(GT911_Device* device) {
     if (device == NULL) {
         return;
     }
     
     // リセットピンが設定されていない場合はソフトリセットを試みる
     if (device->rst_pin == GPIO_NUM_NC) {
         ESP_LOGW(TAG, "Reset pin not set, trying soft reset");
         
         // ソフトリセットのコマンドを送信するコードをここに追加
         // (実際の実装には適切なコマンドが必要)
         return;
     }
     
     // INT信号を入力モードに設定
     gpio_set_direction(device->int_pin, GPIO_MODE_INPUT);
     
     // リセット手順
     gpio_set_level(device->rst_pin, 0);
     vTaskDelay(GT911_RESET_LOW_MS / portTICK_PERIOD_MS);
     
     // INTピンを出力モードに変更してLOWに設定
     gpio_set_direction(device->int_pin, GPIO_MODE_OUTPUT);
     gpio_set_level(device->int_pin, 0);
     vTaskDelay(GT911_INT_LOW_MS / portTICK_PERIOD_MS);
     
     // リセット解除
     gpio_set_level(device->rst_pin, 1);
     vTaskDelay(GT911_RESET_HIGH_MS / portTICK_PERIOD_MS);
     
     // INTピンを入力モードに戻す
     gpio_set_direction(device->int_pin, GPIO_MODE_INPUT);
     vTaskDelay(GT911_RESET_HIGH_MS / portTICK_PERIOD_MS);
     
     ESP_LOGI(TAG, "GT911 reset completed");
 }
 
 /**
  * @brief GT911のステータスをクリアする
  */
 bool gt911_clear_status(GT911_Device* device) {
     if (device == NULL || !device->is_initialized) {
         return false;
     }
     
     // ステータスレジスタに0を書き込み
     uint8_t clear_status = 0;
     return gt911_write_registers(device, GT911_REG_STATUS, &clear_status, 1);
 }
 
 /**
  * @brief GT911の製品IDを取得する
  */
 bool gt911_get_product_id(GT911_Device* device, uint8_t* product_id) {
     if (device == NULL || !device->is_initialized || product_id == NULL) {
         return false;
     }
     
     // 製品IDレジスタから4バイト読み取り
     return gt911_read_registers(device, GT911_REG_PRODUCT_ID, product_id, 4);
 }
 
 /**
  * @brief タッチ感度を設定する (スタブ実装)
  */
 bool gt911_set_sensitivity(GT911_Device* device, uint8_t touch_threshold, uint8_t leave_threshold) {
     // スタブ実装
     ESP_LOGW(TAG, "gt911_set_sensitivity not implemented");
     return false;
 }
 
 /**
  * @brief タッチキーを設定する (スタブ実装)
  */
 bool gt911_configure_touch_keys(GT911_Device* device, bool enable) {
     // スタブ実装
     ESP_LOGW(TAG, "gt911_configure_touch_keys not implemented");
     return false;
 }
 
 /**
  * @brief 特定のタッチキーを設定する (スタブ実装)
  */
 bool gt911_set_touch_key(GT911_Device* device, uint8_t key_id, uint8_t address, uint8_t sensitivity) {
     // スタブ実装
     ESP_LOGW(TAG, "gt911_set_touch_key not implemented");
     return false;
 }
 
 /**
  * @brief リフレッシュレートを設定する (スタブ実装)
  */
 bool gt911_set_refresh_rate(GT911_Device* device, uint8_t rate) {
     // スタブ実装
     ESP_LOGW(TAG, "gt911_set_refresh_rate not implemented");
     return false;
 }
 
 /**
  * @brief 割り込みハンドラ
  */
 static void IRAM_ATTR gt911_interrupt_handler(void* arg) {
     GT911_Device* device = (GT911_Device*)arg;
     
     // セマフォを通知
     BaseType_t xHigherPriorityTaskWoken = pdFALSE;
     xSemaphoreGiveFromISR(device->touch_semaphore, &xHigherPriorityTaskWoken);
     
     // タスク切り替えが必要な場合
     if (xHigherPriorityTaskWoken) {
         portYIELD_FROM_ISR();
     }
 }
 
 /**
  * @brief タッチ処理タスク
  */
 static void gt911_task(void* pvParameters) {
     GT911_Device* device = (GT911_Device*)pvParameters;
     
     while (1) {
         // 割り込み待機（セマフォ待ち）
         if (xSemaphoreTake(device->touch_semaphore, portMAX_DELAY) == pdTRUE) {
             // タッチデータ読み取り
             if (gt911_read_touch_data(device)) {
                 // コールバック関数が設定されていれば呼び出し
                 if (device->callback) {
                     device->callback(device, device->user_data);
                 }
             }
         }
     }
 }
 
 /**
  * @brief チェックサムを計算する
  */
 static bool gt911_calculate_checksum(uint8_t* config, size_t len) {
     if (config == NULL || len < sizeof(gt911_default_config)) {
         return false;
     }
     
     // チェックサム計算
     uint8_t checksum = 0;
     for (size_t i = 0; i < len; i++) {
         checksum += config[i];
     }
     
     // 2の補数
     checksum = (~checksum) + 1;
     
     // チェックサムを設定
     config[len] = checksum;
     
     return true;
 }
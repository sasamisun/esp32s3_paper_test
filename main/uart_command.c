/**
 * @file uart_command.c
 * @brief UART通信を使用したコマンド送受信実装
 */

 #include "uart_command.h"
 #include <stdio.h>
 #include <string.h>
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "freertos/semphr.h"
 #include "esp_log.h"
 #include "esp_system.h"
 #include "driver/uart.h"
 #include "protocol.h"
 
 static const char *TAG = "uart_command";
 
 // タスク・セマフォ
 static TaskHandle_t s_uart_task_handle = NULL;
 static SemaphoreHandle_t s_uart_mutex = NULL;
 
 // コマンドハンドラのコールバック
 static command_handler_t s_command_handler = NULL;
 
 // 受信バッファ
 static uint8_t s_rx_buffer[UART_BUF_SIZE];
 static uint8_t s_packet_buffer[PACKET_BUF_SIZE];
 
 // パケット解析用の状態定義
 typedef enum {
     WAIT_START,
     READ_COMMAND,
     READ_LENGTH_L,
     READ_LENGTH_H,
     READ_DATA,
     READ_CRC_L,
     READ_CRC_H,
     READ_END
 } packet_state_t;
 
 // プロトタイプ宣言
 static void uart_rx_task(void *pvParameters);
 
 /**
  * UART通信モジュールを初期化する
  * @return true: 成功、false: 失敗
  */
 bool uart_command_init(void) {
     // UARTドライバの設定
     uart_config_t uart_config = {
         .baud_rate = UART_BAUD_RATE,
         .data_bits = UART_DATA_8_BITS,
         .parity = UART_PARITY_DISABLE,
         .stop_bits = UART_STOP_BITS_1,
         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
         .source_clk = UART_SCLK_APB,
     };
     
     // UARTドライバインストール
     esp_err_t ret = uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, UART_BUF_SIZE * 2, 0, NULL, 0);
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "UARTドライバインストール失敗: %s", esp_err_to_name(ret));
         return false;
     }
     
     // UARTパラメータ設定
     ret = uart_param_config(UART_NUM, &uart_config);
     if (ret != ESP_OK) {
         ESP_LOGE(TAG, "UARTパラメータ設定失敗: %s", esp_err_to_name(ret));
         uart_driver_delete(UART_NUM);
         return false;
     }
     
     // ミューテックス作成
     s_uart_mutex = xSemaphoreCreateMutex();
     if (s_uart_mutex == NULL) {
         ESP_LOGE(TAG, "UARTミューテックスの作成に失敗しました");
         uart_driver_delete(UART_NUM);
         return false;
     }
     
     ESP_LOGI(TAG, "UART通信モジュールが初期化されました (Baud: %d)", UART_BAUD_RATE);
     return true;
 }
 
 /**
  * UARTモジュールを終了する
  */
 void uart_command_deinit(void) {
     // タスクが実行中なら停止
     if (s_uart_task_handle != NULL) {
         vTaskDelete(s_uart_task_handle);
         s_uart_task_handle = NULL;
     }
     
     // ミューテックスを削除
     if (s_uart_mutex != NULL) {
         vSemaphoreDelete(s_uart_mutex);
         s_uart_mutex = NULL;
     }
     
     // UARTドライバを削除
     uart_driver_delete(UART_NUM);
     
     ESP_LOGI(TAG, "UART通信モジュールが終了しました");
 }
 
 /**
  * CRCを計算する
  * @param data データバッファ
  * @param length データ長
  * @return CRC値
  */
 uint16_t uart_calculate_crc16(const uint8_t *data, int length) {
     // シンプルなCRC-16実装
     uint16_t crc = 0xFFFF;
     
     for (int i = 0; i < length; i++) {
         crc ^= (uint16_t)data[i];
         
         for (int j = 0; j < 8; j++) {
             if (crc & 1) {
                 crc = (crc >> 1) ^ 0xA001; // CRC-16 MODBUS多項式
             } else {
                 crc >>= 1;
             }
         }
     }
     
     return crc;
 }
 
 /**
  * レスポンスパケットを送信する
  * @param resp_code レスポンスコード
  * @param data レスポンスデータ
  * @param data_length データ長
  * @return true: 成功、false: 失敗
  */
 bool uart_send_response(uint8_t resp_code, const void *data, uint16_t data_length) {
     // ミューテックス取得
     if (xSemaphoreTake(s_uart_mutex, pdMS_TO_TICKS(UART_TIMEOUT_MS)) != pdTRUE) {
         ESP_LOGE(TAG, "UARTミューテックス取得失敗");
         return false;
     }
     
     // パケットバッファ準備
     // マーカー(1) + コード(1) + 長さ(2) + データ(n) + CRC(2) + 終了マーカー(1) = データ長 + 7
     size_t packet_size = data_length + 7;
     uint8_t *packet = malloc(packet_size);
     if (packet == NULL) {
         ESP_LOGE(TAG, "メモリ割り当て失敗");
         xSemaphoreGive(s_uart_mutex);
         return false;
     }
     
     int pos = 0;
     
     // 開始マーカー
     packet[pos++] = START_MARKER;
     
     // レスポンスコード
     packet[pos++] = resp_code;
     
     // データ長 (リトルエンディアン)
     packet[pos++] = data_length & 0xFF;
     packet[pos++] = (data_length >> 8) & 0xFF;
     
     // データ (存在する場合)
     if (data_length > 0 && data != NULL) {
         memcpy(packet + pos, data, data_length);
         pos += data_length;
     }
     
     // CRC計算 (コマンドコードからデータまで)
     uint16_t crc = uart_calculate_crc16(packet + 1, pos - 1);
     packet[pos++] = crc & 0xFF;
     packet[pos++] = (crc >> 8) & 0xFF;
     
     // 終了マーカー
     packet[pos++] = END_MARKER;
     
     // 送信
     int sent = uart_write_bytes(UART_NUM, packet, pos);
     free(packet);
     
     // ミューテックス解放
     xSemaphoreGive(s_uart_mutex);
     
     if (sent != pos) {
         ESP_LOGE(TAG, "UART送信エラー: %d/%d バイト", sent, pos);
         return false;
     }
     
     return true;
 }
 
 /**
  * コマンドハンドラを登録する
  * @param handler ハンドラ関数
  */
 void uart_register_command_handler(command_handler_t handler) {
     s_command_handler = handler;
     ESP_LOGI(TAG, "コマンドハンドラが登録されました");
 }
 
 /**
  * UARTモジュールのタスクを開始する
  */
 void uart_command_start(void) {
     if (s_uart_task_handle != NULL) {
         ESP_LOGW(TAG, "UARTタスクは既に実行中です");
         return;
     }
     
     // UARTタスクの作成
     BaseType_t ret = xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 5, &s_uart_task_handle);
     if (ret != pdPASS) {
         ESP_LOGE(TAG, "UARTタスク作成失敗");
         return;
     }
     
     ESP_LOGI(TAG, "UARTタスクが開始されました");
 }
 
 /**
  * UARTパケットを処理する (内部関数)
  * @param command コマンドコード
  * @param data データバッファ
  * @param data_length データ長
  * @param packet_crc パケットのCRC
  * @return true: 正常処理、false: エラー
  */
 static bool process_packet(uint8_t command, const uint8_t *data, uint16_t data_length, uint16_t packet_crc) {
     // CRC検証用バッファを作成
     uint8_t *verify_buffer = malloc(data_length + 1);
     if (verify_buffer == NULL) {
         ESP_LOGE(TAG, "メモリ割り当てエラー");
         return false;
     }
     
     // 検証データを構築 (コマンド + データ)
     verify_buffer[0] = command;
     if (data_length > 0 && data != NULL) {
         memcpy(verify_buffer + 1, data, data_length);
     }
     
     // CRCを計算して検証
     uint16_t calc_crc = uart_calculate_crc16(verify_buffer, data_length + 1);
     free(verify_buffer);
     
     if (calc_crc != packet_crc) {
         ESP_LOGE(TAG, "CRC不一致: 計算=%04X, 受信=%04X", calc_crc, packet_crc);
         return false;
     }
     
     ESP_LOGI(TAG, "コマンド受信: 0x%02X, データ長: %d", command, data_length);
     
     // コマンドハンドラを呼び出す
     if (s_command_handler != NULL) {
         s_command_handler(command, data, data_length);
     } else {
         ESP_LOGW(TAG, "コマンドハンドラが登録されていません");
     }
     
     return true;
 }
 
 /**
  * UARTデータ受信タスク
  * @param pvParameters タスクパラメータ (未使用)
  */
 static void uart_rx_task(void *pvParameters) {
     packet_state_t state = WAIT_START;
     uint8_t command = 0;
     uint16_t data_length = 0;
     uint16_t data_pos = 0;
     uint16_t packet_crc = 0;
     
     ESP_LOGI(TAG, "UART受信タスク開始");
     
     while (1) {
         // UARTからデータを読み取る
         int len = uart_read_bytes(UART_NUM, s_rx_buffer, UART_BUF_SIZE, pdMS_TO_TICKS(100));
         
         if (len <= 0) {
             // タイムアウトまたはエラー、続行
             continue;
         }
         
         // 受信データを1バイトずつ処理
         for (int i = 0; i < len; i++) {
             uint8_t c = s_rx_buffer[i];
             
             switch (state) {
                 case WAIT_START:
                     // 開始マーカーを待機
                     if (c == START_MARKER) {
                         state = READ_COMMAND;
                     }
                     break;
                     
                 case READ_COMMAND:
                     // コマンドコードを読み取り
                     command = c;
                     state = READ_LENGTH_L;
                     break;
                     
                 case READ_LENGTH_L:
                     // データ長下位バイトを読み取り
                     data_length = c;
                     state = READ_LENGTH_H;
                     break;
                     
                 case READ_LENGTH_H:
                     // データ長上位バイトを読み取り
                     data_length |= (c << 8);
                     data_pos = 0;
                     
                     if (data_length > 0) {
                         // データがある場合は読み取り
                         if (data_length > PACKET_BUF_SIZE) {
                             ESP_LOGE(TAG, "データサイズが大きすぎます: %d", data_length);
                             state = WAIT_START;
                         } else {
                             state = READ_DATA;
                         }
                     } else {
                         // データがない場合はCRCへ
                         state = READ_CRC_L;
                     }
                     break;
                     
                 case READ_DATA:
                     // データバイトを読み取り
                     s_packet_buffer[data_pos++] = c;
                     if (data_pos >= data_length) {
                         state = READ_CRC_L;
                     }
                     break;
                     
                 case READ_CRC_L:
                     // CRC下位バイトを読み取り
                     packet_crc = c;
                     state = READ_CRC_H;
                     break;
                     
                 case READ_CRC_H:
                     // CRC上位バイトを読み取り
                     packet_crc |= (c << 8);
                     state = READ_END;
                     break;
                     
                 case READ_END:
                     // 終了マーカーを確認
                     if (c == END_MARKER) {
                         // パケット完了、データを処理
                         if (!process_packet(command, s_packet_buffer, data_length, packet_crc)) {
                             // エラーレスポンス送信
                             uart_send_response(RESP_ERROR, NULL, 0);
                         }
                     } else {
                         ESP_LOGE(TAG, "不正なパケット終了マーカー: %02X", c);
                         uart_send_response(RESP_ERROR, NULL, 0);
                     }
                     // 次のパケットを待機
                     state = WAIT_START;
                     break;
             }
         }
     }
 }
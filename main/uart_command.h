#ifndef UART_COMMAND_H
#define UART_COMMAND_H

#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"

// UART設定
#define UART_NUM            UART_NUM_0
#define UART_BAUD_RATE      115200
#define UART_TIMEOUT_MS     1000

/**
 * UART通信モジュールを初期化する
 * @return true: 成功、false: 失敗
 */
bool uart_command_init(void);

/**
 * UARTモジュールを終了する
 */
void uart_command_deinit(void);

/**
 * レスポンスパケットを送信する
 * @param resp_code レスポンスコード
 * @param data レスポンスデータ
 * @param data_length データ長
 * @return true: 成功、false: 失敗
 */
bool uart_send_response(uint8_t resp_code, const void *data, uint16_t data_length);

/**
 * コマンド受信関数の型定義
 * @param command コマンドコード
 * @param data コマンドデータ
 * @param data_length データ長
 */
typedef void (*command_handler_t)(uint8_t command, const uint8_t *data, uint16_t data_length);

/**
 * コマンドハンドラを登録する
 * @param handler ハンドラ関数
 */
void uart_register_command_handler(command_handler_t handler);

/**
 * UARTモジュールのタスクを開始する
 */
void uart_command_start(void);

/**
 * CRCを計算する
 * @param data データバッファ
 * @param length データ長
 * @return CRC値
 */
uint16_t uart_calculate_crc16(const uint8_t *data, int length);

#endif /* UART_COMMAND_H */
#ifndef COMMAND_HANDLERS_H
#define COMMAND_HANDLERS_H

#include <stdint.h>

/**
 * コマンドハンドラモジュールを初期化する
 */
void command_handlers_init(void);

/**
 * コマンドルーティング関数
 * @param command コマンドコード
 * @param data コマンドデータ
 * @param data_length データ長
 */
void command_handler_process(uint8_t command, const uint8_t *data, uint16_t data_length);

/**
 * デバイス状態確認（PING）コマンドハンドラ
 */
void handle_ping(const uint8_t *data, uint16_t data_length);

/**
 * デバイスリセットコマンドハンドラ
 */
void handle_reset(const uint8_t *data, uint16_t data_length);

/**
 * ファイル一覧取得コマンドハンドラ
 */
void handle_file_list(const uint8_t *data, uint16_t data_length);

/**
 * ファイル情報取得コマンドハンドラ
 */
void handle_file_info(const uint8_t *data, uint16_t data_length);

/**
 * ファイル存在確認コマンドハンドラ
 */
void handle_file_exist(const uint8_t *data, uint16_t data_length);

/**
 * ファイルオープンコマンドハンドラ
 */
void handle_file_open(const uint8_t *data, uint16_t data_length);

/**
 * ファイルデータ転送コマンドハンドラ
 */
void handle_file_data(const uint8_t *data, uint16_t data_length);

/**
 * ファイルクローズコマンドハンドラ
 */
void handle_file_close(const uint8_t *data, uint16_t data_length);

/**
 * ファイル削除コマンドハンドラ
 */
void handle_file_delete(const uint8_t *data, uint16_t data_length);

/**
 * ディレクトリ作成コマンドハンドラ
 */
void handle_dir_create(const uint8_t *data, uint16_t data_length);

/**
 * ディレクトリ削除コマンドハンドラ
 */
void handle_dir_delete(const uint8_t *data, uint16_t data_length);

#endif /* COMMAND_HANDLERS_H */
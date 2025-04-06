#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

// パケットマーカー
#define START_MARKER 0xAA
#define END_MARKER   0x55

// バッファサイズの定義
#define MAX_PATH_LENGTH     256
#define MAX_DATA_SIZE       4096
#define UART_BUF_SIZE       4096
#define PACKET_BUF_SIZE     8192

// 基本コマンド
#define CMD_PING              0x01  // デバイス状態確認
#define CMD_RESET             0x02  // デバイスリセット

// ファイル操作コマンド
#define CMD_FILE_LIST         0x10  // ファイル/フォルダ一覧取得
#define CMD_FILE_INFO         0x11  // ファイル情報取得
#define CMD_FILE_EXIST        0x12  // ファイル存在確認

// ファイル転送コマンド
#define CMD_FILE_OPEN         0x20  // ファイル転送開始
#define CMD_FILE_DATA         0x21  // ファイルデータ転送
#define CMD_FILE_CLOSE        0x22  // ファイル転送終了

// ファイル管理コマンド
#define CMD_FILE_DELETE       0x30  // ファイル削除
#define CMD_DIR_CREATE        0x31  // フォルダ作成
#define CMD_DIR_DELETE        0x32  // フォルダ削除（再帰的）

// レスポンスコード
#define RESP_OK               0xE0  // 成功
#define RESP_ERROR            0xE1  // 一般エラー
#define RESP_FILE_NOT_FOUND   0xE2  // ファイルが見つからない
#define RESP_DISK_FULL        0xE3  // ディスクフル
#define RESP_INVALID_PARAM    0xE4  // 不正なパラメータ

// パケット構造体
typedef struct {
    uint8_t command;
    uint16_t data_length;
    uint8_t *data;
    uint16_t crc;
} command_packet_t;

typedef struct {
    uint8_t response;
    uint16_t data_length;
    uint8_t *data;
    uint16_t crc;
} response_packet_t;

#endif /* PROTOCOL_H */
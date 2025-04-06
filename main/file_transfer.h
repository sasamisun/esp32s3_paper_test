#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

#include <stdio.h>
#include <stdbool.h>

/**
 * ファイル転送モジュールを初期化する
 */
void file_transfer_init(void);

/**
 * ファイルを開く
 * @param path ファイルパス
 * @param mode オープンモード (0=読込, 1=書込, 2=追記)
 * @return true: 成功、false: 失敗
 */
bool file_transfer_open(const char *path, uint8_t mode);

/**
 * ファイルからデータを読み込む
 * @param buffer 読み込みバッファ
 * @param size 読み込むサイズ
 * @param read_size 実際に読み込んだサイズ
 * @param eof ファイル終端に達したらtrue
 * @return true: 成功、false: 失敗
 */
bool file_transfer_read(uint8_t *buffer, uint16_t size, uint16_t *read_size, bool *eof);

/**
 * ファイルにデータを書き込む
 * @param data 書き込むデータ
 * @param size データサイズ
 * @return true: 成功、false: 失敗
 */
bool file_transfer_write(const uint8_t *data, uint16_t size);

/**
 * 現在開いているファイルを閉じる
 * @return true: 成功、false: 失敗
 */
bool file_transfer_close(void);

/**
 * 現在開いているファイルの情報を取得する
 * @param path ファイルパス（出力）
 * @param max_path パスバッファサイズ
 * @param is_open ファイルが開いているかどうか
 * @param mode オープンモード
 * @return true: 成功、false: 失敗
 */
bool file_transfer_get_status(char *path, size_t max_path, bool *is_open, uint8_t *mode);

#endif /* FILE_TRANSFER_H */
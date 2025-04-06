#ifndef SDCARD_MANAGER_H
#define SDCARD_MANAGER_H

#include <stdio.h>
#include <stdbool.h>
#include "esp_err.h"

// SDカードマウントするパス
#define MOUNT_POINT "/sdcard"

/**
 * SDカードを初期化・マウントする
 * @return ESP_OK: 成功、それ以外: エラー
 */
esp_err_t sdcard_init(void);

/**
 * SDカードをアンマウントする
 */
void sdcard_deinit(void);

/**
 * SDカードの空き容量とトータルサイズを取得する
 * @param total_bytes 総容量（バイト）
 * @param free_bytes 空き容量（バイト）
 * @return true: 成功、false: 失敗
 */
bool sdcard_get_info(uint64_t *total_bytes, uint64_t *free_bytes);

/**
 * 指定されたパスの存在を確認する
 * @param path チェックするパス（SDカードのルートからの相対パス）
 * @return true: 存在する、false: 存在しない
 */
bool sdcard_path_exists(const char *path);

/**
 * 指定されたパスがディレクトリかどうかを確認する
 * @param path チェックするパス
 * @return true: ディレクトリである、false: ディレクトリでない
 */
bool sdcard_is_dir(const char *path);

/**
 * フォルダを作成する
 * @param path 作成するフォルダのパス
 * @return true: 成功、false: 失敗
 */
bool sdcard_mkdir(const char *path);

/**
 * ファイル/フォルダを削除する（フォルダは空である必要がある）
 * @param path 削除するパス
 * @return true: 成功、false: 失敗
 */
bool sdcard_remove(const char *path);

/**
 * フォルダを再帰的に削除する（中身ごと削除）
 * @param path 削除するフォルダのパス
 * @return true: 成功、false: 失敗
 */
bool sdcard_rmdir_recursive(const char *path);

/**
 * SDカードのパスを完全なパスに変換する
 * @param rel_path 相対パス
 * @param full_path 完全なパスが格納されるバッファ
 * @param max_len バッファの最大長
 * @return true: 成功、false: 失敗
 */
bool sdcard_get_full_path(const char *rel_path, char *full_path, size_t max_len);

#endif /* SDCARD_MANAGER_H */
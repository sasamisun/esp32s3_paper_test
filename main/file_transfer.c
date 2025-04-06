/**
 * @file file_transfer.c
 * @brief ファイル転送モジュールの実装
 */

#include "file_transfer.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h> // stat構造体と関連関数のために追加
#include <unistd.h>   // 一部のファイル操作関数
#include <dirent.h>   // ディレクトリ操作
#include "esp_log.h"
#include "sdcard_manager.h"
#include "protocol.h"

static const char *TAG = "file_transfer";

// ファイル転送セッション情報
typedef struct
{
    FILE *file;                     // 現在開いているファイルハンドル
    char filename[MAX_PATH_LENGTH]; // 現在開いているファイル名
    uint8_t mode;                   // ファイルオープンモード（0=読込, 1=書込, 2=追記）
    bool is_open;                   // ファイルがオープンされているかどうか
} file_session_t;

// ファイル転送セッション
static file_session_t s_session = {0};

/**
 * ファイル転送モジュールを初期化する
 */
void file_transfer_init(void)
{
    // セッション情報を初期化
    s_session.file = NULL;
    s_session.filename[0] = '\0';
    s_session.mode = 0;
    s_session.is_open = false;

    ESP_LOGI(TAG, "ファイル転送モジュールが初期化されました");
}

/**
 * ファイルを開く
 * @param path ファイルパス
 * @param mode オープンモード (0=読込, 1=書込, 2=追記)
 * @return true: 成功、false: 失敗
 */
bool file_transfer_open(const char *path, uint8_t mode)
{
    // すでにファイルが開いている場合は閉じる
    if (s_session.is_open && s_session.file != NULL)
    {
        fclose(s_session.file);
        s_session.file = NULL;
        s_session.is_open = false;
    }

    if (path == NULL)
    {
        ESP_LOGE(TAG, "無効なパス");
        return false;
    }

    // モードの検証
    if (mode > 2)
    {
        ESP_LOGE(TAG, "無効なファイルオープンモード: %d", mode);
        return false;
    }

    // SDカード上の完全なパスを構築
    char full_path[MAX_PATH_LENGTH];
    if (!sdcard_get_full_path(path, full_path, sizeof(full_path)))
    {
        ESP_LOGE(TAG, "パス構築失敗: %s", path);
        return false;
    }

    // ファイルオープンモードを設定
    const char *mode_str;
    switch (mode)
    {
    case 0:
        mode_str = "rb";
        break; // 読込
    case 1:
        mode_str = "wb";
        break; // 書込
    case 2:
        mode_str = "ab";
        break; // 追記
    default:
        ESP_LOGE(TAG, "未定義のモード: %d", mode);
        return false;
    }

    // 読み込みモードの場合はファイルの存在確認
    if (mode == 0 && !sdcard_path_exists(path))
    {
        ESP_LOGE(TAG, "ファイルが存在しません: %s", path);
        return false;
    }

    // 書き込み/追記モードの場合はディレクトリの存在確認
    if ((mode == 1 || mode == 2))
    {
        // パスからディレクトリ部分を抽出
        char dir_path[MAX_PATH_LENGTH] = {0};
        char *last_slash = strrchr(full_path, '/');
        if (last_slash != NULL)
        {
            size_t dir_len = last_slash - full_path;
            strncpy(dir_path, full_path, dir_len);
            dir_path[dir_len] = '\0';

            // ディレクトリが存在しない場合は作成を試みる
            struct stat st;
            if (stat(dir_path, &st) != 0 || !S_ISDIR(st.st_mode))
            {
                ESP_LOGW(TAG, "ディレクトリが存在しません。作成を試みます: %s", dir_path);
                // ディレクトリ作成（相対パスに戻す必要がある）
                char rel_dir[MAX_PATH_LENGTH] = {0};
                size_t mount_point_len = strlen(MOUNT_POINT);
                if (strncmp(dir_path, MOUNT_POINT, mount_point_len) == 0)
                {
                    // マウントポイントを削除して相対パスに変換
                    if (dir_path[mount_point_len] == '/')
                    {
                        strcpy(rel_dir, dir_path + mount_point_len + 1);
                    }
                    else
                    {
                        strcpy(rel_dir, dir_path + mount_point_len);
                    }
                }

                if (!sdcard_mkdir(rel_dir))
                {
                    ESP_LOGE(TAG, "ディレクトリ作成失敗: %s", rel_dir);
                    return false;
                }
            }
        }
    }

    // ファイルを開く
    FILE *file = fopen(full_path, mode_str);
    if (file == NULL)
    {
        ESP_LOGE(TAG, "ファイルオープン失敗: %s, モード: %s", full_path, mode_str);
        return false;
    }

    // セッション情報を更新
    s_session.file = file;
    strncpy(s_session.filename, full_path, sizeof(s_session.filename) - 1);
    s_session.filename[sizeof(s_session.filename) - 1] = '\0';
    s_session.mode = mode;
    s_session.is_open = true;

    ESP_LOGI(TAG, "ファイルをオープンしました: %s, モード: %s", full_path, mode_str);
    return true;
}

/**
 * ファイルからデータを読み込む
 * @param buffer 読み込みバッファ
 * @param size 読み込むサイズ
 * @param read_size 実際に読み込んだサイズ
 * @param eof ファイル終端に達したらtrue
 * @return true: 成功、false: 失敗
 */
bool file_transfer_read(uint8_t *buffer, uint16_t size, uint16_t *read_size, bool *eof)
{
    if (buffer == NULL || read_size == NULL || eof == NULL)
    {
        ESP_LOGE(TAG, "無効なパラメータ");
        return false;
    }

    // ファイルが開いていることを確認
    if (!s_session.is_open || s_session.file == NULL)
    {
        ESP_LOGE(TAG, "ファイルが開かれていません");
        return false;
    }

    // 読み込みモードで開かれていることを確認
    if (s_session.mode != 0)
    {
        ESP_LOGE(TAG, "ファイルは読み込みモードで開かれていません");
        return false;
    }

    // ファイルからデータを読み込む
    *read_size = fread(buffer, 1, size, s_session.file);

    // ファイル終端をチェック
    *eof = feof(s_session.file) ? true : false;

    if (*read_size == 0 && !(*eof))
    {
        ESP_LOGE(TAG, "ファイル読み込みエラー");
        return false;
    }

    return true;
}

/**
 * ファイルにデータを書き込む
 * @param data 書き込むデータ
 * @param size データサイズ
 * @return true: 成功、false: 失敗
 */
bool file_transfer_write(const uint8_t *data, uint16_t size)
{
    if (data == NULL || size == 0)
    {
        ESP_LOGE(TAG, "無効なパラメータ");
        return false;
    }

    // ファイルが開いていることを確認
    if (!s_session.is_open || s_session.file == NULL)
    {
        ESP_LOGE(TAG, "ファイルが開かれていません");
        return false;
    }

    // 書き込みまたは追記モードで開かれていることを確認
    if (s_session.mode != 1 && s_session.mode != 2)
    {
        ESP_LOGE(TAG, "ファイルは書き込みモードで開かれていません");
        return false;
    }

    // ファイルにデータを書き込む
    size_t written = fwrite(data, 1, size, s_session.file);

    if (written != size)
    {
        ESP_LOGE(TAG, "ファイル書き込みエラー: %d/%d バイト", written, size);
        return false;
    }

    // バッファをフラッシュして確実にディスクに書き込む
    fflush(s_session.file);

    return true;
}

/**
 * 現在開いているファイルを閉じる
 * @return true: 成功、false: 失敗
 */
bool file_transfer_close(void)
{
    // ファイルが開いていない場合は何もしない
    if (!s_session.is_open || s_session.file == NULL)
    {
        ESP_LOGW(TAG, "閉じるファイルがありません");
        return true;
    }

    // ファイルを閉じる
    if (fclose(s_session.file) != 0)
    {
        ESP_LOGE(TAG, "ファイルクローズエラー: %s", s_session.filename);
        s_session.file = NULL;
        s_session.is_open = false;
        return false;
    }

    // セッション情報をクリア
    s_session.file = NULL;
    s_session.filename[0] = '\0';
    s_session.mode = 0;
    s_session.is_open = false;

    ESP_LOGI(TAG, "ファイルを閉じました");
    return true;
}

/**
 * 現在開いているファイルの情報を取得する
 * @param path ファイルパス（出力）
 * @param max_path パスバッファサイズ
 * @param is_open ファイルが開いているかどうか
 * @param mode オープンモード
 * @return true: 成功、false: 失敗
 */
bool file_transfer_get_status(char *path, size_t max_path, bool *is_open, uint8_t *mode)
{
    if (path == NULL || max_path == 0 || is_open == NULL || mode == NULL)
    {
        ESP_LOGE(TAG, "無効なパラメータ");
        return false;
    }

    // 現在のセッション情報をコピー
    *is_open = s_session.is_open;
    *mode = s_session.mode;

    if (s_session.is_open)
    {
        strncpy(path, s_session.filename, max_path - 1);
        path[max_path - 1] = '\0';
    }
    else
    {
        path[0] = '\0';
    }

    return true;
}
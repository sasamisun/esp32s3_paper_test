/**
 * @file command_handlers.c
 * @brief コマンド処理ハンドラの実装
 */

#include "command_handlers.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdcard_manager.h"
#include "uart_command.h"
#include "file_transfer.h"
#include "protocol.h"

static const char *TAG = "cmd_handlers";

/**
 * コマンドハンドラモジュールを初期化する
 */
void command_handlers_init(void)
{
    ESP_LOGI(TAG, "コマンドハンドラモジュールが初期化されました");
}

/**
 * コマンドルーティング関数
 * @param command コマンドコード
 * @param data コマンドデータ
 * @param data_length データ長
 */
void command_handler_process(uint8_t command, const uint8_t *data, uint16_t data_length)
{
    ESP_LOGI(TAG, "コマンド処理: 0x%02X, データ長: %d", command, data_length);

    switch (command)
    {
    case CMD_PING:
        handle_ping(data, data_length);
        break;

    case CMD_RESET:
        handle_reset(data, data_length);
        break;

    case CMD_FILE_LIST:
        handle_file_list(data, data_length);
        break;

    case CMD_FILE_INFO:
        handle_file_info(data, data_length);
        break;

    case CMD_FILE_EXIST:
        handle_file_exist(data, data_length);
        break;

    case CMD_FILE_OPEN:
        handle_file_open(data, data_length);
        break;

    case CMD_FILE_DATA:
        handle_file_data(data, data_length);
        break;

    case CMD_FILE_CLOSE:
        handle_file_close(data, data_length);
        break;

    case CMD_FILE_DELETE:
        handle_file_delete(data, data_length);
        break;

    case CMD_DIR_CREATE:
        handle_dir_create(data, data_length);
        break;

    case CMD_DIR_DELETE:
        handle_dir_delete(data, data_length);
        break;

    default:
        ESP_LOGW(TAG, "不明なコマンド: 0x%02X", command);
        uart_send_response(RESP_INVALID_PARAM, NULL, 0);
        break;
    }
}

/**
 * デバイス状態確認（PING）コマンドハンドラ
 */
void handle_ping(const uint8_t *data, uint16_t data_length)
{
    // デバイス状態情報構造体
    typedef struct
    {
        uint32_t heap_free;      // 空きヒープメモリ（バイト）
        uint8_t sd_mounted;      // SDカードマウント状態（0=未マウント, 1=マウント済）
        uint64_t sd_total_space; // SDカード総容量（バイト）
        uint64_t sd_free_space;  // SDカード空き容量（バイト）
        uint32_t uptime;         // 起動時間（秒）
    } device_status_t;

    device_status_t status;
    memset(&status, 0, sizeof(status));

    // ヒープ情報を取得
    status.heap_free = esp_get_free_heap_size();

    // SDカード情報を取得
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;

    if (sdcard_get_info(&total_bytes, &free_bytes))
    {
        status.sd_mounted = 1;
        status.sd_total_space = total_bytes;
        status.sd_free_space = free_bytes;
    }
    else
    {
        status.sd_mounted = 0;
    }

    // 起動時間を取得
    // esp_timer_get_timeの代わりにxTaskGetTickCountを使用
    status.uptime = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000); // ミリ秒→秒

    ESP_LOGI(TAG, "PING応答: ヒープ=%lu バイト, SDカード=%s, 起動時間=%lu 秒",
             status.heap_free,
             status.sd_mounted ? "マウント済" : "未マウント",
             status.uptime);

    // レスポンス送信
    uart_send_response(RESP_OK, &status, sizeof(status));
}

/**
 * デバイスリセットコマンドハンドラ
 */
void handle_reset(const uint8_t *data, uint16_t data_length)
{
    ESP_LOGI(TAG, "デバイスリセットリクエスト受信");

    // オープン中のファイルを閉じる
    file_transfer_close();

    // レスポンス送信
    uart_send_response(RESP_OK, NULL, 0);

    // 少し遅延させてからリセット実行
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "デバイスリセット実行");
    esp_restart();
}

/**
 * ファイル一覧取得コマンドハンドラ
 */
void handle_file_list(const uint8_t *data, uint16_t data_length)
{
    // NULL終端されたパス文字列を作成
    char *path = NULL;

    if (data_length > 0)
    {
        path = malloc(data_length + 1);
        if (path == NULL)
        {
            ESP_LOGE(TAG, "メモリ割り当て失敗");
            uart_send_response(RESP_ERROR, NULL, 0);
            return;
        }

        memcpy(path, data, data_length);
        path[data_length] = '\0';
    }
    else
    {
        // パスが指定されていない場合はルートディレクトリを使用
        path = strdup("/");
        if (path == NULL)
        {
            ESP_LOGE(TAG, "メモリ割り当て失敗");
            uart_send_response(RESP_ERROR, NULL, 0);
            return;
        }
    }

    // SDカード上のフルパスを取得
    char full_path[MAX_PATH_LENGTH];
    if (!sdcard_get_full_path(path, full_path, sizeof(full_path)))
    {
        ESP_LOGE(TAG, "パス変換失敗: %s", path);
        free(path);
        uart_send_response(RESP_INVALID_PARAM, NULL, 0);
        return;
    }

    free(path); // 元のパスメモリは不要

    // ディレクトリを開く
    DIR *dir = opendir(full_path);
    if (dir == NULL)
    {
        ESP_LOGE(TAG, "ディレクトリを開けません: %s", full_path);
        uart_send_response(RESP_FILE_NOT_FOUND, NULL, 0);
        return;
    }

    // ファイル一覧をバッファに格納
    uint8_t *resp_buffer = malloc(PACKET_BUF_SIZE);
    if (resp_buffer == NULL)
    {
        ESP_LOGE(TAG, "レスポンスバッファのメモリ割り当て失敗");
        closedir(dir);
        uart_send_response(RESP_ERROR, NULL, 0);
        return;
    }

    uint16_t resp_pos = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL)
    {
        // 「.」と「..」は除外
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        // ファイル情報取得
        char item_path[MAX_PATH_LENGTH];
        // より安全なパス結合
        size_t path_len = strlen(full_path);
        size_t entry_name_len = strlen(entry->d_name);

        // オーバーフローチェック
        if (path_len + entry_name_len + 2 > sizeof(item_path))
        {
            ESP_LOGW(TAG, "パス名が長すぎます: %s/%s", full_path, entry->d_name);
            continue;
        }

        // パスを安全に結合
        strcpy(item_path, full_path);
        if (full_path[path_len - 1] != '/')
        {
            strcat(item_path, "/");
        }
        strcat(item_path, entry->d_name);

        struct stat st;
        if (stat(item_path, &st) != 0)
        {
            ESP_LOGW(TAG, "項目の情報取得失敗: %s", item_path);
            continue;
        }
        
        // ファイル情報を追加
        uint8_t type = S_ISDIR(st.st_mode) ? 1 : 0; // 0=ファイル, 1=ディレクトリ
        uint32_t size = S_ISDIR(st.st_mode) ? 0 : st.st_size;
        uint32_t mtime = st.st_mtime;
        uint8_t name_len = strlen(entry->d_name);

        // バッファサイズチェック
        if (resp_pos + 10 + name_len > PACKET_BUF_SIZE)
        {
            ESP_LOGW(TAG, "レスポンスバッファが一杯です、残りを省略");
            break;
        }

        // バッファに追加
        resp_buffer[resp_pos++] = type;

        // サイズ（リトルエンディアン）
        resp_buffer[resp_pos++] = size & 0xFF;
        resp_buffer[resp_pos++] = (size >> 8) & 0xFF;
        resp_buffer[resp_pos++] = (size >> 16) & 0xFF;
        resp_buffer[resp_pos++] = (size >> 24) & 0xFF;

        // 更新日時（リトルエンディアン）
        resp_buffer[resp_pos++] = mtime & 0xFF;
        resp_buffer[resp_pos++] = (mtime >> 8) & 0xFF;
        resp_buffer[resp_pos++] = (mtime >> 16) & 0xFF;
        resp_buffer[resp_pos++] = (mtime >> 24) & 0xFF;

        // 名前長
        resp_buffer[resp_pos++] = name_len;

        // 名前
        memcpy(resp_buffer + resp_pos, entry->d_name, name_len);
        resp_pos += name_len;
    }

    closedir(dir);

    ESP_LOGI(TAG, "ファイル一覧: %d 項目, %d バイト", resp_pos / 10, resp_pos);

    // レスポンス送信
    uart_send_response(RESP_OK, resp_buffer, resp_pos);
    free(resp_buffer);
}

/**
 * ファイル情報取得コマンドハンドラ
 */
void handle_file_info(const uint8_t *data, uint16_t data_length)
{
    if (data_length == 0)
    {
        ESP_LOGE(TAG, "パスが指定されていません");
        uart_send_response(RESP_INVALID_PARAM, NULL, 0);
        return;
    }

    // NULL終端されたパス文字列を作成
    char *path = malloc(data_length + 1);
    if (path == NULL)
    {
        ESP_LOGE(TAG, "メモリ割り当て失敗");
        uart_send_response(RESP_ERROR, NULL, 0);
        return;
    }

    memcpy(path, data, data_length);
    path[data_length] = '\0';

    // SDカード上のフルパスを取得
    char full_path[MAX_PATH_LENGTH];
    if (!sdcard_get_full_path(path, full_path, sizeof(full_path)))
    {
        ESP_LOGE(TAG, "パス変換失敗: %s", path);
        free(path);
        uart_send_response(RESP_INVALID_PARAM, NULL, 0);
        return;
    }

    free(path);

    // ファイル情報取得
    struct stat st;
    if (stat(full_path, &st) != 0)
    {
        ESP_LOGE(TAG, "ファイル情報取得失敗: %s", full_path);
        uart_send_response(RESP_FILE_NOT_FOUND, NULL, 0);
        return;
    }

    // ファイル情報構造体
    typedef struct
    {
        uint8_t type;      // 0=ファイル, 1=ディレクトリ
        uint32_t size;     // ファイルサイズ
        uint32_t created;  // 作成日時
        uint32_t modified; // 更新日時
    } file_info_t;

    file_info_t info;
    info.type = S_ISDIR(st.st_mode) ? 1 : 0;
    info.size = S_ISDIR(st.st_mode) ? 0 : st.st_size;
    info.created = st.st_ctime;
    info.modified = st.st_mtime;

    ESP_LOGI(TAG, "ファイル情報: %s, タイプ=%s, サイズ=%lu バイト",
             full_path,
             info.type ? "ディレクトリ" : "ファイル",
             info.size);

    // レスポンス送信
    uart_send_response(RESP_OK, &info, sizeof(info));
}

/**
 * ファイル存在確認コマンドハンドラ
 */
void handle_file_exist(const uint8_t *data, uint16_t data_length)
{
    if (data_length == 0)
    {
        ESP_LOGE(TAG, "パスが指定されていません");
        uart_send_response(RESP_INVALID_PARAM, NULL, 0);
        return;
    }

    // NULL終端されたパス文字列を作成
    char *path = malloc(data_length + 1);
    if (path == NULL)
    {
        ESP_LOGE(TAG, "メモリ割り当て失敗");
        uart_send_response(RESP_ERROR, NULL, 0);
        return;
    }

    memcpy(path, data, data_length);
    path[data_length] = '\0';

    // 存在確認
    bool exists = sdcard_path_exists(path);

    if (exists)
    {
        // ディレクトリかどうかも確認
        bool is_dir = sdcard_is_dir(path);
        uint8_t result[2] = {1, is_dir ? 1 : 0}; // [0]=存在, [1]=ディレクトリ

        ESP_LOGI(TAG, "存在確認: %s - %s", path, is_dir ? "ディレクトリ" : "ファイル");
        uart_send_response(RESP_OK, result, sizeof(result));
    }
    else
    {
        uint8_t result[2] = {0, 0}; // 存在しない

        ESP_LOGI(TAG, "存在確認: %s - 存在しません", path);
        uart_send_response(RESP_OK, result, sizeof(result));
    }

    free(path);
}

/**
 * ファイルオープンコマンドハンドラ
 */
void handle_file_open(const uint8_t *data, uint16_t data_length)
{
    if (data_length < 2)
    {
        ESP_LOGE(TAG, "パラメータが不足しています");
        uart_send_response(RESP_INVALID_PARAM, NULL, 0);
        return;
    }

    // モード取得（1バイト目: 0=読込, 1=書込, 2=追記）
    uint8_t mode = data[0];

    // NULL終端されたパス文字列を作成
    char *path = malloc(data_length);
    if (path == NULL)
    {
        ESP_LOGE(TAG, "メモリ割り当て失敗");
        uart_send_response(RESP_ERROR, NULL, 0);
        return;
    }

    memcpy(path, data + 1, data_length - 1);
    path[data_length - 1] = '\0';

    // ファイルを開く
    bool success = file_transfer_open(path, mode);
    free(path);

    if (success)
    {
        ESP_LOGI(TAG, "ファイルオープン成功");
        uart_send_response(RESP_OK, NULL, 0);
    }
    else
    {
        ESP_LOGE(TAG, "ファイルオープン失敗");
        uart_send_response(RESP_FILE_NOT_FOUND, NULL, 0);
    }
}

/**
 * ファイルデータ転送コマンドハンドラ
 */
void handle_file_data(const uint8_t *data, uint16_t data_length)
{
    if (data_length < 1)
    {
        ESP_LOGE(TAG, "パラメータが不足しています");
        uart_send_response(RESP_INVALID_PARAM, NULL, 0);
        return;
    }

    // 方向取得（0=読込, 1=書込）
    uint8_t direction = data[0];

    if (direction == 0)
    { // 読込モード
        // 読み込みサイズ取得
        uint16_t max_read_size = MAX_DATA_SIZE;

        if (data_length >= 3)
        {
            max_read_size = data[1] | (data[2] << 8);
            // 最大データサイズを超えないように調整
            if (max_read_size > MAX_DATA_SIZE)
            {
                max_read_size = MAX_DATA_SIZE;
            }
        }

        // データバッファを確保
        uint8_t *buffer = malloc(max_read_size);
        if (buffer == NULL)
        {
            ESP_LOGE(TAG, "メモリ割り当て失敗");
            uart_send_response(RESP_ERROR, NULL, 0);
            return;
        }

        // ファイルからデータを読み込む
        uint16_t read_size = 0;
        bool eof = false;

        if (!file_transfer_read(buffer, max_read_size, &read_size, &eof))
        {
            ESP_LOGE(TAG, "ファイル読み込み失敗");
            free(buffer);
            uart_send_response(RESP_ERROR, NULL, 0);
            return;
        }

        // 読み取りデータ＋EOFフラグを送信
        uint8_t *resp = malloc(read_size + 1);
        if (resp == NULL)
        {
            ESP_LOGE(TAG, "メモリ割り当て失敗");
            free(buffer);
            uart_send_response(RESP_ERROR, NULL, 0);
            return;
        }

        resp[0] = eof ? 1 : 0;
        memcpy(resp + 1, buffer, read_size);

        ESP_LOGI(TAG, "ファイル読み込み: %d バイト, EOF=%d", read_size, eof);
        uart_send_response(RESP_OK, resp, read_size + 1);

        free(buffer);
        free(resp);
    }
    else if (direction == 1)
    { // 書込モード
        // データ書き込み
        if (data_length <= 1)
        {
            ESP_LOGE(TAG, "書き込むデータがありません");
            uart_send_response(RESP_INVALID_PARAM, NULL, 0);
            return;
        }

        // ファイルにデータを書き込む
        if (!file_transfer_write(data + 1, data_length - 1))
        {
            ESP_LOGE(TAG, "ファイル書き込み失敗");
            uart_send_response(RESP_ERROR, NULL, 0);
            return;
        }

        ESP_LOGI(TAG, "ファイル書き込み: %d バイト", data_length - 1);
        uart_send_response(RESP_OK, NULL, 0);
    }
    else
    {
        ESP_LOGE(TAG, "不正な方向: %d", direction);
        uart_send_response(RESP_INVALID_PARAM, NULL, 0);
    }
}

/**
 * ファイルクローズコマンドハンドラ
 */
void handle_file_close(const uint8_t *data, uint16_t data_length)
{
    // 現在開いているファイルを閉じる
    bool success = file_transfer_close();

    if (success)
    {
        ESP_LOGI(TAG, "ファイルクローズ成功");
        uart_send_response(RESP_OK, NULL, 0);
    }
    else
    {
        ESP_LOGE(TAG, "ファイルクローズ失敗");
        uart_send_response(RESP_ERROR, NULL, 0);
    }
}

/**
 * ファイル削除コマンドハンドラ
 */
void handle_file_delete(const uint8_t *data, uint16_t data_length)
{
    if (data_length == 0)
    {
        ESP_LOGE(TAG, "パスが指定されていません");
        uart_send_response(RESP_INVALID_PARAM, NULL, 0);
        return;
    }

    // NULL終端されたパス文字列を作成
    char *path = malloc(data_length + 1);
    if (path == NULL)
    {
        ESP_LOGE(TAG, "メモリ割り当て失敗");
        uart_send_response(RESP_ERROR, NULL, 0);
        return;
    }

    memcpy(path, data, data_length);
    path[data_length] = '\0';

    // ファイル削除
    bool success = sdcard_remove(path);

    if (success)
    {
        ESP_LOGI(TAG, "ファイル削除成功: %s", path);
        uart_send_response(RESP_OK, NULL, 0);
    }
    else
    {
        ESP_LOGE(TAG, "ファイル削除失敗: %s", path);
        uart_send_response(RESP_ERROR, NULL, 0);
    }

    free(path);
}

/**
 * ディレクトリ作成コマンドハンドラ
 */
void handle_dir_create(const uint8_t *data, uint16_t data_length)
{
    if (data_length == 0)
    {
        ESP_LOGE(TAG, "パスが指定されていません");
        uart_send_response(RESP_INVALID_PARAM, NULL, 0);
        return;
    }

    // NULL終端されたパス文字列を作成
    char *path = malloc(data_length + 1);
    if (path == NULL)
    {
        ESP_LOGE(TAG, "メモリ割り当て失敗");
        uart_send_response(RESP_ERROR, NULL, 0);
        return;
    }

    memcpy(path, data, data_length);
    path[data_length] = '\0';

    // ディレクトリ作成
    bool success = sdcard_mkdir(path);

    if (success)
    {
        ESP_LOGI(TAG, "ディレクトリ作成成功: %s", path);
        uart_send_response(RESP_OK, NULL, 0);
    }
    else
    {
        ESP_LOGE(TAG, "ディレクトリ作成失敗: %s", path);
        uart_send_response(RESP_ERROR, NULL, 0);
    }

    free(path);
}

/**
 * ディレクトリ削除コマンドハンドラ
 */
void handle_dir_delete(const uint8_t *data, uint16_t data_length)
{
    if (data_length == 0)
    {
        ESP_LOGE(TAG, "パスが指定されていません");
        uart_send_response(RESP_INVALID_PARAM, NULL, 0);
        return;
    }

    // NULL終端されたパス文字列を作成
    char *path = malloc(data_length + 1);
    if (path == NULL)
    {
        ESP_LOGE(TAG, "メモリ割り当て失敗");
        uart_send_response(RESP_ERROR, NULL, 0);
        return;
    }

    memcpy(path, data, data_length);
    path[data_length] = '\0';

    // ディレクトリ削除（再帰的）
    bool success = sdcard_rmdir_recursive(path);

    if (success)
    {
        ESP_LOGI(TAG, "ディレクトリ削除成功: %s", path);
        uart_send_response(RESP_OK, NULL, 0);
    }
    else
    {
        ESP_LOGE(TAG, "ディレクトリ削除失敗: %s", path);
        uart_send_response(RESP_ERROR, NULL, 0);
    }

    free(path);
}
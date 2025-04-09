/**
 * @file sdcard_manager.c
 * @brief SDカード管理モジュールの実装
 */

#include "sdcard_manager.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "protocol.h"

static const char *TAG = "sdcard_manager";

// SDカード情報
static sdmmc_card_t *s_card = NULL;
static bool s_is_mounted = false;

/**
 * SDカードを初期化・マウントする
 * @return ESP_OK: 成功、それ以外: エラー
 */
esp_err_t sdcard_init(void)
{
    if (s_is_mounted)
    {
        ESP_LOGW(TAG, "SDカードはすでにマウントされています");
        return ESP_OK;
    }

    esp_err_t ret;

    // SDカード設定
    ESP_LOGI(TAG, "SDカードを初期化します");

    // M5PaperS3の正しいピン設定
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    
    // SDMMCホストの設定カスタマイズ
    host.flags = SDMMC_HOST_FLAG_4BIT;
    host.slot = SDMMC_HOST_SLOT_1;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    
    // スロット設定
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    
    // M5PaperS3の正しいピン設定
    slot_config.clk = GPIO_NUM_39;    // SCK
    slot_config.cmd = GPIO_NUM_38;    // MOSI/CMD
    slot_config.d0 = GPIO_NUM_40;     // MISO/D0
    slot_config.d1 = GPIO_NUM_NC;     // 未使用
    slot_config.d2 = GPIO_NUM_NC;     // 未使用
    slot_config.d3 = GPIO_NUM_47;     // CS/D3
    slot_config.width = 1;            // 1-bit SDモード
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // マウント設定
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,  // マウント失敗時にフォーマットしない
        .max_files = 10,                  // 同時にオープンできるファイル数
        .allocation_unit_size = 16 * 1024 // クラスタサイズ 16KB
    };

    // SDカードをマウント
    ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);

    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "SDカードのマウントに失敗しました。SDカードをフォーマットしてください。");
        }
        else
        {
            ESP_LOGE(TAG, "SDカードの初期化に失敗しました: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    s_is_mounted = true;

    // SDカード情報の表示
    ESP_LOGI(TAG, "SDカード情報:");
    ESP_LOGI(TAG, "名前: %s", s_card->cid.name);
    ESP_LOGI(TAG, "容量: %lluMB", ((uint64_t)s_card->csd.capacity) * s_card->csd.sector_size / (1024 * 1024));
    ESP_LOGI(TAG, "バス幅: %d-bit", s_card->log_bus_width);

    return ESP_OK;
}

/**
 * SDカードをアンマウントする
 */
void sdcard_deinit(void)
{
    if (!s_is_mounted)
    {
        ESP_LOGW(TAG, "SDカードはマウントされていません");
        return;
    }

    ESP_LOGI(TAG, "SDカードをアンマウントします");
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_card);
    s_card = NULL;
    s_is_mounted = false;
    ESP_LOGI(TAG, "SDカードのアンマウントが完了しました");
}

/**
 * SDカードの空き容量とトータルサイズを取得する
 * @param total_bytes 総容量（バイト）
 * @param free_bytes 空き容量（バイト）
 * @return true: 成功、false: 失敗
 */
bool sdcard_get_info(uint64_t *total_bytes, uint64_t *free_bytes)
{
    if (!s_is_mounted || !total_bytes || !free_bytes)
    {
        return false;
    }

    FATFS *fs;
    DWORD free_clusters, total_clusters, sector_size;

    // FATFSファイルシステム情報の取得
    BYTE pdrv = 0;
    char drv[3] = {MOUNT_POINT[0], ':', 0}; // 例: "0:"

    if (f_getfree(drv, &free_clusters, &fs) != FR_OK)
    {
        ESP_LOGE(TAG, "f_getfree失敗");
        return false;
    }

    total_clusters = (fs->n_fatent - 2); // ルートディレクトリと使用不可クラスタを除外
    sector_size = s_card->csd.sector_size;

    *total_bytes = ((uint64_t)total_clusters) * fs->csize * sector_size;
    *free_bytes = ((uint64_t)free_clusters) * fs->csize * sector_size;

    return true;
}

/**
 * 指定されたパスの存在を確認する
 * @param path チェックするパス（SDカードのルートからの相対パス）
 * @return true: 存在する、false: 存在しない
 */
bool sdcard_path_exists(const char *path)
{
    if (!s_is_mounted || !path)
    {
        return false;
    }

    char full_path[MAX_PATH_LENGTH];
    if (!sdcard_get_full_path(path, full_path, sizeof(full_path)))
    {
        return false;
    }

    struct stat st;
    return (stat(full_path, &st) == 0);
}

/**
 * 指定されたパスがディレクトリかどうかを確認する
 * @param path チェックするパス
 * @return true: ディレクトリである、false: ディレクトリでない
 */
bool sdcard_is_dir(const char *path)
{
    if (!s_is_mounted || !path)
    {
        return false;
    }

    char full_path[MAX_PATH_LENGTH];
    if (!sdcard_get_full_path(path, full_path, sizeof(full_path)))
    {
        return false;
    }

    struct stat st;
    if (stat(full_path, &st) != 0)
    {
        return false;
    }

    return S_ISDIR(st.st_mode);
}

/**
 * フォルダを作成する
 * @param path 作成するフォルダのパス
 * @return true: 成功、false: 失敗
 */
bool sdcard_mkdir(const char *path)
{
    if (!s_is_mounted || !path)
    {
        return false;
    }

    char full_path[MAX_PATH_LENGTH];
    if (!sdcard_get_full_path(path, full_path, sizeof(full_path)))
    {
        return false;
    }

    // すでに存在するか確認
    if (sdcard_path_exists(path))
    {
        if (sdcard_is_dir(path))
        {
            // すでにディレクトリが存在する
            return true;
        }
        else
        {
            // 同名のファイルが存在する
            ESP_LOGE(TAG, "同名のファイルが存在します: %s", path);
            return false;
        }
    }

    // 再帰的にディレクトリを作成
    if (mkdir(full_path, 0755) != 0)
    {
        ESP_LOGE(TAG, "ディレクトリ作成失敗: %s", full_path);
        return false;
    }

    return true;
}

/**
 * ファイル/フォルダを削除する（フォルダは空である必要がある）
 * @param path 削除するパス
 * @return true: 成功、false: 失敗
 */
bool sdcard_remove(const char *path)
{
    if (!s_is_mounted || !path)
    {
        return false;
    }

    char full_path[MAX_PATH_LENGTH];
    if (!sdcard_get_full_path(path, full_path, sizeof(full_path)))
    {
        return false;
    }

    // 存在確認
    if (!sdcard_path_exists(path))
    {
        ESP_LOGE(TAG, "指定されたパスが存在しません: %s", path);
        return false;
    }

    // ディレクトリかファイルかチェック
    if (sdcard_is_dir(path))
    {
        // ディレクトリの場合はrmdir
        if (rmdir(full_path) != 0)
        {
            ESP_LOGE(TAG, "ディレクトリ削除失敗: %s", full_path);
            return false;
        }
    }
    else
    {
        // ファイルの場合はunlink
        if (unlink(full_path) != 0)
        {
            ESP_LOGE(TAG, "ファイル削除失敗: %s", full_path);
            return false;
        }
    }

    return true;
}

/**
 * ディレクトリ内のファイルを再帰的に削除する（内部関数）
 * @param path 削除するディレクトリのパス
 * @return true: 成功、false: 失敗
 */
static bool delete_directory_contents(const char *path)
{
    DIR *dir = opendir(path);
    if (!dir)
    {
        ESP_LOGE(TAG, "ディレクトリを開けません: %s", path);
        return false;
    }

    struct dirent *entry;
    bool result = true;

    while ((entry = readdir(dir)) != NULL)
    {
        // "." と ".." は無視
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        // パスを構築
        char full_path[MAX_PATH_LENGTH];
        // より安全なパス結合
        size_t path_len = strlen(path);
        size_t name_len = strlen(entry->d_name);

        // オーバーフローチェック
        if (path_len + name_len + 2 > sizeof(full_path))
        {
            ESP_LOGW(TAG, "パス名が長すぎます: %s/%s", path, entry->d_name);
            result = false;
            continue;
        }

        // パスを安全に結合
        strcpy(full_path, path);
        if (path[path_len - 1] != '/')
        {
            strcat(full_path, "/");
        }
        strcat(full_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0)
        {
            ESP_LOGE(TAG, "ファイル情報の取得に失敗: %s", full_path);
            result = false;
            continue;
        }

        if (S_ISDIR(st.st_mode))
        {
            // サブディレクトリを再帰的に削除
            if (!delete_directory_contents(full_path))
            {
                result = false;
                continue;
            }
            if (rmdir(full_path) != 0)
            {
                ESP_LOGE(TAG, "サブディレクトリの削除に失敗: %s", full_path);
                result = false;
            }
        }
        else
        {
            // ファイルを削除
            if (unlink(full_path) != 0)
            {
                ESP_LOGE(TAG, "ファイル削除に失敗: %s", full_path);
                result = false;
            }
        }
    }

    closedir(dir);
    return result;
}

/**
 * フォルダを再帰的に削除する（中身ごと削除）
 * @param path 削除するフォルダのパス
 * @return true: 成功、false: 失敗
 */
bool sdcard_rmdir_recursive(const char *path)
{
    if (!s_is_mounted || !path)
    {
        return false;
    }

    char full_path[MAX_PATH_LENGTH];
    if (!sdcard_get_full_path(path, full_path, sizeof(full_path)))
    {
        return false;
    }

    // 存在確認
    if (!sdcard_path_exists(path))
    {
        ESP_LOGE(TAG, "指定されたパスが存在しません: %s", path);
        return false;
    }

    // ディレクトリかチェック
    if (!sdcard_is_dir(path))
    {
        ESP_LOGE(TAG, "指定されたパスがディレクトリではありません: %s", path);
        return false;
    }

    // ディレクトリ内のファイルを全て削除
    if (!delete_directory_contents(full_path))
    {
        ESP_LOGE(TAG, "ディレクトリ内のファイル削除に一部失敗しました: %s", path);
        return false;
    }

    // 空になったディレクトリを削除
    if (rmdir(full_path) != 0)
    {
        ESP_LOGE(TAG, "ディレクトリ削除失敗: %s", full_path);
        return false;
    }

    return true;
}

/**
 * SDカードのパスを完全なパスに変換する
 * @param rel_path 相対パス
 * @param full_path 完全なパスが格納されるバッファ
 * @param max_len バッファの最大長
 * @return true: 成功、false: 失敗
 */
bool sdcard_get_full_path(const char *rel_path, char *full_path, size_t max_len)
{
    if (!rel_path || !full_path || max_len == 0)
    {
        return false;
    }

    // 先頭のスラッシュがあるかチェック
    bool has_leading_slash = (rel_path[0] == '/');

    // マウントポイントと相対パスを結合
    int written = snprintf(full_path, max_len, "%s%s%s",
                           MOUNT_POINT,
                           has_leading_slash ? "" : "/",
                           has_leading_slash ? rel_path + 1 : rel_path);

    if (written < 0 || written >= max_len)
    {
        ESP_LOGE(TAG, "パス名が長すぎます: %s", rel_path);
        return false;
    }

    return true;
}
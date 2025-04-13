/**
 * @file usb_msc.c
 * @brief USB Mass Storage Class (MSC) over SPI SD Card implementation
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "driver/gpio.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"

#include "usb_msc.h"

//#include "esp_system.h" // MACアドレス取得に必要
#include "esp_mac.h" // MACアドレス取得のためのヘッダ（新しいESP-IDF）

// SDカードピン定義
#define PIN_NUM_MISO GPIO_NUM_40
#define PIN_NUM_MOSI GPIO_NUM_38
#define PIN_NUM_CLK GPIO_NUM_39
#define PIN_NUM_CS GPIO_NUM_47

// SPI定義
#define SD_SPI_HOST SPI2_HOST

static const char *TAG = "usb_msc";
static sdmmc_card_t *s_card = NULL;
static const char s_mount_point[] = "/sdcard";
static bool s_msc_initialized = false;
static bool s_sd_initialized = false;

// USBディスクリプタ設定
#define EPNUM_MSC 1
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN)

enum
{
    ITF_NUM_MSC = 0,
    ITF_NUM_TOTAL
};

enum
{
    EDPT_CTRL_OUT = 0x00,
    EDPT_CTRL_IN = 0x80,
    EDPT_MSC_OUT = 0x01,
    EDPT_MSC_IN = 0x81,
};

// シリアル番号用の文字列バッファ
static char serial_str[20];

// 文字列記述子配列の宣言
static const char lang_id[] = {0x09, 0x04};
static const char *manufacturer = "M5Paper S3";
static const char *product = "M5Paper S3 Storage";
static char const *string_desc_arr[4];

static tusb_desc_device_t descriptor_config = {
    .bLength = sizeof(descriptor_config),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,  // Espressif VID
    .idProduct = 0x5001, // M5Paper S3専用のID
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01};

// MACアドレスを使ってシリアル番号を生成する関数
static void generate_serial_from_mac(void)
{
    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC address: %s", esp_err_to_name(ret));
        // エラー時は代替のシリアル番号を使用
        strcpy(serial_str, "M5P3-UNKNOWN");
    } else {
        // MACアドレスを16進数文字列に変換
        snprintf(serial_str, sizeof(serial_str), "%02X%02X%02X%02X%02X%02X", 
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    snprintf(serial_str, sizeof(serial_str), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    string_desc_arr[0] = lang_id;
    string_desc_arr[1] = manufacturer;
    string_desc_arr[2] = product;
    string_desc_arr[3] = serial_str;

    ESP_LOGI(TAG, "Generated USB serial number from MAC: %s", serial_str);
}

static uint8_t const msc_configuration_desc[] = {
    // Config number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, EP Out & EP In address, EP size
    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EDPT_MSC_OUT, EDPT_MSC_IN, 64),
};

// MSCマウント状態変更コールバック
static void storage_mount_changed_cb(tinyusb_msc_event_t *event)
{
    ESP_LOGI(TAG, "Storage mounted to application: %s", event->mount_changed_data.is_mounted ? "Yes" : "No");
}

esp_err_t usb_msc_init_sd_card(void)
{
    if (s_sd_initialized)
    {
        ESP_LOGI(TAG, "SD card already initialized");
        return ESP_OK;
    }

    esp_err_t ret;

    // SDカード設定
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};

    // SPI設定
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    // SPI初期化
    ret = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    // SDSPI設定
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;
    slot_config.host_id = SD_SPI_HOST;

    // SDカードマウント
    ESP_LOGI(TAG, "Mounting SD card...");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = slot_config.host_id;

    ret = esp_vfs_fat_sdspi_mount(s_mount_point, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK)
    {
        if (ret == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount filesystem");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    // カード情報表示
    sdmmc_card_print_info(stdout, s_card);
    s_sd_initialized = true;
    return ESP_OK;
}

esp_err_t usb_msc_init(void)
{
    if (s_msc_initialized)
    {
        ESP_LOGI(TAG, "USB MSC already initialized");
        return ESP_OK;
    }

    if (!s_sd_initialized)
    {
        ESP_LOGE(TAG, "SD card not initialized. Call usb_msc_init_sd_card first");
        return ESP_ERR_INVALID_STATE;
    }

    // MACアドレスからシリアル番号を生成
    generate_serial_from_mac();

    // TinyUSB MSC SDカード設定
    const tinyusb_msc_sdmmc_config_t config_sdmmc = {
        .card = s_card,
        .callback_mount_changed = storage_mount_changed_cb,
        .mount_config.max_files = 5,
    };

    // TinyUSB MSC SDカード初期化
    esp_err_t ret = tinyusb_msc_storage_init_sdmmc(&config_sdmmc);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize TinyUSB MSC SDMMC: %s", esp_err_to_name(ret));
        return ret;
    }

    // TinyUSB設定
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &descriptor_config,
        .string_descriptor = string_desc_arr,
        .string_descriptor_count = sizeof(string_desc_arr) / sizeof(string_desc_arr[0]),
        .external_phy = false,
        .configuration_descriptor = msc_configuration_desc,
    };

    // TinyUSBドライバのインストール
    ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to install TinyUSB driver: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "USB MSC initialized successfully");
    s_msc_initialized = true;
    return ESP_OK;
}

esp_err_t usb_msc_unmount_card(void)
{
    if (!s_msc_initialized)
    {
        ESP_LOGE(TAG, "USB MSC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Unmounting SD card from application to allow USB host access...");
    esp_err_t ret = tinyusb_msc_storage_unmount();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to unmount storage: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SD card unmounted. USB host can now access it");
    return ESP_OK;
}

esp_err_t usb_msc_mount_card(void)
{
    if (!s_msc_initialized)
    {
        ESP_LOGE(TAG, "USB MSC not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Mounting SD card for application access...");
    esp_err_t ret = tinyusb_msc_storage_mount(s_mount_point);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount storage: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted at %s", s_mount_point);
    return ESP_OK;
}

bool usb_msc_host_using_storage(void)
{
    if (!s_msc_initialized)
    {
        return false;
    }

    return tinyusb_msc_storage_in_use_by_usb_host();
}

esp_err_t usb_msc_deinit(void)
{
    if (!s_msc_initialized)
    {
        return ESP_OK;
    }

    // MSCをアンマウント
    esp_err_t ret = tinyusb_msc_storage_unmount();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to unmount storage: %s", esp_err_to_name(ret));
        // 続行する
    }

    // MSCのコールバック登録解除
    tinyusb_msc_unregister_callback(TINYUSB_MSC_EVENT_MOUNT_CHANGED);

    // MSCストレージを解放 - 戻り値なし
    tinyusb_msc_storage_deinit();

    // TinyUSBドライバをアンインストール
    ret = tinyusb_driver_uninstall();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to uninstall TinyUSB driver: %s", esp_err_to_name(ret));
        // 続行する
    }

    // SDカードをアンマウント
    if (s_sd_initialized && s_card != NULL)
    {
        esp_vfs_fat_sdcard_unmount(s_mount_point, s_card);
        s_card = NULL;
    }

    // SPIバスを解放
    ret = spi_bus_free(SD_SPI_HOST);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to free SPI bus: %s", esp_err_to_name(ret));
        // 続行する
    }

    s_msc_initialized = false;
    s_sd_initialized = false;
    ESP_LOGI(TAG, "USB MSC deinitialized");

    return ESP_OK;
}

const char *usb_msc_get_mount_point(void)
{
    return s_mount_point;
}
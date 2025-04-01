/**
 * @file gt911.c
 * @brief GT911タッチコントローラドライバの実装
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

#include "gt911.h"

static const char *TAG = "gt911";

static gt911_touch_callback_t touch_callback = NULL;
static SemaphoreHandle_t gt911_semaphore = NULL;


static void gt911_dump_registers(GT911_Device* device);
esp_err_t gt911_i2c_scan(i2c_port_t i2c_port);
bool gt911_read_config(GT911_Device* device);
void gt911_explore_registers(GT911_Device* device);

/**
 * @brief I2Cからデータを書き込む関数
 */
static esp_err_t gt911_i2c_write(GT911_Device *device, uint16_t reg_addr, uint8_t *data, size_t data_len)
{
    uint8_t write_buf[2 + data_len];
    write_buf[0] = (reg_addr >> 8) & 0xFF; // レジスタアドレス上位バイト
    write_buf[1] = reg_addr & 0xFF;        // レジスタアドレス下位バイト
    if (data && data_len > 0)
    {
        memcpy(&write_buf[2], data, data_len);
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, device->i2c_addr | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, write_buf, 2 + data_len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(device->i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    return ret;
}

/**
 * @brief I2Cからデータを読み込む関数
 */
static esp_err_t gt911_i2c_read(GT911_Device *device, uint16_t reg_addr, uint8_t *data, size_t data_len)
{
    if (data == NULL || data_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // レジスタアドレスを送信
    uint8_t write_buf[2] = {(reg_addr >> 8) & 0xFF, reg_addr & 0xFF};

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device->i2c_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, write_buf, 2, true);

    // 読み取りモードでリスタート
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (device->i2c_addr << 1) | I2C_MASTER_READ, true);
    if (data_len > 1)
    {
        i2c_master_read(cmd, data, data_len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + data_len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(device->i2c_port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C read failed: %s (addr=0x%02X, reg=0x%04X)",
                 esp_err_to_name(ret), device->i2c_addr, reg_addr);
    }
    else
    {
        ESP_LOGI(TAG, "I2C read success: addr=0x%02X, reg=0x%04X, data[0]=0x%02X",
                 device->i2c_addr, reg_addr, data[0]);
    }

    return ret;
}

/**
 * @brief タッチ割り込みハンドラ
 */
static void IRAM_ATTR gt911_touch_intr_handler(void *arg)
{
    if (gt911_semaphore != NULL)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(gt911_semaphore, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken)
        {
            portYIELD_FROM_ISR();
        }
    }
}

/**
 * @brief タッチデータ処理タスク
 */
static void gt911_touch_task(void *pvParameters)
{
    GT911_Device *device = (GT911_Device *)pvParameters;

    while (1)
    {
        // 割り込み待ち
        if (xSemaphoreTake(gt911_semaphore, portMAX_DELAY) == pdTRUE)
        {
            if (gt911_read_touch_data(device) && touch_callback != NULL)
            {
                touch_callback(device);
            }
        }
    }
}

/**
 * @brief GT911の初期化
 */
bool gt911_init(GT911_Device *device, gpio_num_t sda_pin, gpio_num_t scl_pin,
                gpio_num_t int_pin, gpio_num_t rst_pin)
{
    if (device == NULL)
    {
        ESP_LOGE(TAG, "Invalid device pointer");
        return false;
    }

    // 構造体を初期化
    memset(device, 0, sizeof(GT911_Device));
    device->i2c_port = GT911_I2C_PORT;
    device->i2c_addr = GT911_I2C_ADDR_DEFAULT;
    device->int_pin = int_pin;
    device->rst_pin = rst_pin;

    // I2C設定
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_pin,
        .scl_io_num = scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000 // 400kHz
    };

    esp_err_t ret = i2c_param_config(device->i2c_port, &i2c_conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C parameter configuration failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = i2c_driver_install(device->i2c_port, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C driver installation failed: %s", esp_err_to_name(ret));
        return false;
    }

    // リセットピンの設定（使用する場合）
    if (rst_pin != GPIO_NUM_NC)
    {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << rst_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE};
        gpio_config(&io_conf);
        gpio_set_level(rst_pin, 1); // リセット解除
    }

    // 割り込みピンの設定
    gpio_config_t intr_conf = {
        .pin_bit_mask = (1ULL << int_pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE // 立ち下がりエッジで割り込み
    };
    gpio_config(&intr_conf);

    // セマフォの作成
    gt911_semaphore = xSemaphoreCreateBinary();
    if (gt911_semaphore == NULL)
    {
        ESP_LOGE(TAG, "Failed to create semaphore");
        i2c_driver_delete(device->i2c_port);
        return false;
    }

    // 割り込みハンドラの登録
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "Failed to install ISR service: %s", esp_err_to_name(err));
        vSemaphoreDelete(gt911_semaphore);
        i2c_driver_delete(device->i2c_port);
        return false;
    }

    err = gpio_isr_handler_add(int_pin, gt911_touch_intr_handler, NULL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add ISR handler: %s", esp_err_to_name(err));
        vSemaphoreDelete(gt911_semaphore);
        i2c_driver_delete(device->i2c_port);
        return false;
    }

    // タッチ処理タスクの作成
    BaseType_t xReturned = xTaskCreate(gt911_touch_task, "gt911_task", 4096, device, 10, NULL);
    if (xReturned != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create GT911 task");
        gpio_isr_handler_remove(int_pin);
        vSemaphoreDelete(gt911_semaphore);
        i2c_driver_delete(device->i2c_port);
        return false;
    }

    // GT911デバイスの初期化確認
    // レジスタ確認などの追加検証コードをここに実装可能
    // I2C初期化後にスキャンチェックを実行
    gt911_i2c_scan(device->i2c_port);
    gt911_dump_registers(device);
    gt911_read_config(device);
    gt911_explore_registers(device);
    
    device->is_initialized = true;
    ESP_LOGI(TAG, "GT911 initialized successfully");
    return true;
}

/**
 * @brief GT911を終了し、リソースを解放する
 */
void gt911_deinit(GT911_Device *device)
{
    if (device == NULL || !device->is_initialized)
    {
        return;
    }

    // 割り込みハンドラの削除
    gpio_isr_handler_remove(device->int_pin);

    // I2Cドライバの解放
    i2c_driver_delete(device->i2c_port);

    // セマフォの削除（グローバル変数のためデバイスごとではない）
    if (gt911_semaphore != NULL)
    {
        vSemaphoreDelete(gt911_semaphore);
        gt911_semaphore = NULL;
    }

    device->is_initialized = false;
    ESP_LOGI(TAG, "GT911 deinitialized");
}

/**
 * @brief タッチデータを読み取る
 */
bool gt911_read_touch_data(GT911_Device* device) {
    // ステータスレジスタの読み取り
    uint8_t status;
    // 仕様書からの正確なステータスレジスタアドレスを使用
    esp_err_t ret = gt911_i2c_read(device, 0x814E, &status, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read status register: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "Status register (0x814E): 0x%02X", status);
    
    // 0x814Eのステータスレジスタの下位4ビットがタッチポイント数
    uint8_t touch_points = status & 0x0F;
    if (touch_points > GT911_MAX_TOUCH_POINTS) {
        ESP_LOGW(TAG, "Invalid touch point count: %d, limiting to %d", 
                touch_points, GT911_MAX_TOUCH_POINTS);
        touch_points = GT911_MAX_TOUCH_POINTS;
    }
    
    device->active_points = touch_points;
    
    if (touch_points == 0) {
        // クリア操作：タッチデータを読み取った後、ステータスレジスタをクリア
        uint8_t clear_reg = 0;
        gt911_i2c_write(device, 0x814E, &clear_reg, 1);
        return false;
    }
    
    // タッチポイントデータの読み取り
    // 仕様書に従って正確なタッチポイントデータのアドレスを使用
    for (int i = 0; i < touch_points; i++) {
        // 各タッチポイントのレジスタアドレス
        uint16_t point_addr = 0x8150 + i * 8;
        uint8_t point_data[8];
        
        if (gt911_i2c_read(device, point_addr, point_data, sizeof(point_data)) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read touch point %d data", i);
            continue;
        }
        
        // タッチポイントデータの解析
        // 仕様書に基づき、最初の2バイトがX座標、次の2バイトがY座標
        device->points[i].x = ((uint16_t)point_data[1] << 8) | point_data[0];
        device->points[i].y = ((uint16_t)point_data[3] << 8) | point_data[2];
        device->points[i].size = ((uint16_t)point_data[5] << 8) | point_data[4];
        device->points[i].tracking_id = point_data[7];
        device->points[i].is_pressed = true;
        
        ESP_LOGI(TAG, "Touch point %d: x=%d, y=%d, size=%d", 
                i, device->points[i].x, device->points[i].y, device->points[i].size);
    }
    
    // 非アクティブなポイントをリセット
    for (int i = touch_points; i < GT911_MAX_TOUCH_POINTS; i++) {
        device->points[i].is_pressed = false;
    }
    
    // ステータスレジスタをクリア
    uint8_t clear_reg = 0;
    gt911_i2c_write(device, 0x814E, &clear_reg, 1);
    
    return true;
}

/**
 * @brief コールバック関数を登録する
 */
void gt911_register_callback(GT911_Device *device, gt911_touch_callback_t callback)
{
    if (device != NULL && device->is_initialized)
    {
        touch_callback = callback;
    }
}

/**
 * @brief GT911をスリープモードに切り替える
 */
bool gt911_enter_sleep_mode(GT911_Device *device)
{
    if (device == NULL || !device->is_initialized)
    {
        ESP_LOGE(TAG, "Device not initialized");
        return false;
    }

    // スリープモードへの移行コマンドを送信 (具体的なコマンドはデータシート参照)
    uint8_t cmd = 0x05; // スリープコマンド(例示用)
    esp_err_t ret = gt911_i2c_write(device, 0x8040, &cmd, 1);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enter sleep mode: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "GT911 entered sleep mode");
    return true;
}

/**
 * @brief GT911をスリープモードから復帰させる
 */
bool gt911_exit_sleep_mode(GT911_Device *device)
{
    if (device == NULL || !device->is_initialized)
    {
        ESP_LOGE(TAG, "Device not initialized");
        return false;
    }

    // INT ピンを High に設定して wake-up シーケンスを開始
    gpio_set_level(device->int_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(5)); // 2〜5msのパルス
    gpio_set_level(device->int_pin, 0);

    vTaskDelay(pdMS_TO_TICKS(100)); // ウェイクアップのための十分な時間を確保

    ESP_LOGI(TAG, "GT911 woken up from sleep mode");
    return true;
}

// レジスタのダンプ
static void gt911_dump_registers(GT911_Device* device) {
    ESP_LOGI(TAG, "Dumping GT911 registers:");
    
    // ステータス領域
    uint8_t status_data[16];
    if (gt911_i2c_read(device, 0x8140, status_data, sizeof(status_data)) == ESP_OK) {
        ESP_LOGI(TAG, "Status registers (0x8140-0x814F):");
        for (int i = 0; i < sizeof(status_data); i++) {
            printf("%02X ", status_data[i]);
            if ((i + 1) % 8 == 0) printf("\n");
        }
        printf("\n");
    }
    
    // タッチポイントデータ領域
    uint8_t point_data[40];
    if (gt911_i2c_read(device, 0x8150, point_data, sizeof(point_data)) == ESP_OK) {
        ESP_LOGI(TAG, "Touch point registers (0x8150-0x8177):");
        for (int i = 0; i < sizeof(point_data); i++) {
            printf("%02X ", point_data[i]);
            if ((i + 1) % 8 == 0) printf("\n");
        }
        printf("\n");
    }
}

// I2Cスキャンテスト
esp_err_t gt911_i2c_scan(i2c_port_t i2c_port)
{
    printf("Scanning I2C bus...\n");

    uint8_t device_count = 0;
    for (uint8_t i = 3; i < 0x78; i++)
    {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (i << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(i2c_port, cmd, 50 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK)
        {
            printf("Found device at address 0x%02X\n", i);
            device_count++;
        }
    }

    printf("Scan complete, found %d devices\n", device_count);
    return (device_count > 0) ? ESP_OK : ESP_FAIL;
}

// GT911の設定情報を読み取り
bool gt911_read_config(GT911_Device* device) {
    uint8_t config_data[4];
    
    // X/Y解像度の読み取り
    if (gt911_i2c_read(device, 0x8048, config_data, 4) == ESP_OK) {
        uint16_t x_max = ((uint16_t)config_data[1] << 8) | config_data[0];
        uint16_t y_max = ((uint16_t)config_data[3] << 8) | config_data[2];
        ESP_LOGI(TAG, "Touch panel resolution: %dx%d", x_max, y_max);
    }
    
    // タッチポイント数の読み取り
    if (gt911_i2c_read(device, 0x804C, config_data, 1) == ESP_OK) {
        ESP_LOGI(TAG, "Supported touch points: %d", config_data[0] & 0x0F);
    }
    
    // 座標反転設定の読み取り
    if (gt911_i2c_read(device, 0x804D, config_data, 1) == ESP_OK) {
        bool x_reversed = (config_data[0] & 0x40) != 0;
        bool y_reversed = (config_data[0] & 0x80) != 0;
        bool xy_swapped = (config_data[0] & 0x08) != 0;
        ESP_LOGI(TAG, "Coordinate config: X reversed: %d, Y reversed: %d, XY swapped: %d",
                x_reversed, y_reversed, xy_swapped);
    }
    
    return true;
}
// レジスタ検索
void gt911_explore_registers(GT911_Device* device) {
    ESP_LOGI(TAG, "Exploring GT911 registers...");
    
    // 製品IDを読む
    uint8_t id_buf[4];
    if (gt911_i2c_read(device, 0x8140, id_buf, 4) == ESP_OK) {
        ESP_LOGI(TAG, "ID at 0x8140: %02X %02X %02X %02X", 
                id_buf[0], id_buf[1], id_buf[2], id_buf[3]);
    }
    
    // ステータス関連レジスタの範囲を探索
    for (uint16_t addr = 0x8140; addr <= 0x8150; addr++) {
        uint8_t val;
        if (gt911_i2c_read(device, addr, &val, 1) == ESP_OK) {
            ESP_LOGI(TAG, "Reg 0x%04X = 0x%02X", addr, val);
        }
    }
    
    // タッチポイントデータの範囲を探索
    for (uint16_t addr = 0x8150; addr <= 0x8170; addr += 8) {
        uint8_t buf[8];
        if (gt911_i2c_read(device, addr, buf, 8) == ESP_OK) {
            ESP_LOGI(TAG, "Data at 0x%04X: %02X %02X %02X %02X %02X %02X %02X %02X",
                    addr, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
        }
    }
}
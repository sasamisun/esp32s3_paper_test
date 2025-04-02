# M5Paper S3用タッチスクリーン実装仕様書

## 1. 概要

本仕様書は、M5Paper S3デバイスにおけるGoodix GT911タッチコントローラの実装方法について説明します。タッチスクリーンの接続方法、必要な初期設定、および提供される機能について詳細に記述しています。

## 2. ハードウェア接続

M5Paper S3とGT911タッチコントローラは以下のように接続されています：

| GT911ピン | M5Paper S3ピン | 機能 |
|----------|--------------|------|
| VDD      | 3.3V         | 電源 (3.3V) |
| GND      | GND          | グラウンド |
| SCL      | GPIO 42      | I²Cクロック |
| SDA      | GPIO 41      | I²Cデータ |
| INT      | GPIO 48      | 割り込み信号 |
| RSTB     | 外部プルアップ | リセット信号 (10kΩプルアップ) |
| VDDIO    | 3.3V         | I/O電源 |

### 2.1 I²C設定

- **I²Cポート**: I2C_NUM_0
- **I²C周波数**: 400kHz
- **I²Cアドレス**: 0x5D (7ビットアドレス、0xBA/0xBB 8ビット形式)
- **プルアップ抵抗**: 外部10kΩプルアップ抵抗をSDA、SCL、INTピンに接続

### 2.2 回路図

```
    3.3V
     │
     ├── 10kΩ     ┌─── M5Paper S3 ───┐
     │            │                  │
     ├── 10kΩ     │                  │
     │            │                  │
┌────┴───────┐    │                  │
│    VDD     │    │                  │
│            │    │                  │
│    SCL     ├────┤ GPIO 42          │
│            │    │                  │
│    SDA     ├────┤ GPIO 41          │
│            │    │                  │
│    INT     ├────┤ GPIO 48          │
│            │    │                  │
│    RSTB    ├────┤ 外部プルアップ     │
│            │    │                  │
│    VDDIO   ├────┤ 3.3V             │
│            │    │                  │
│    GND     ├────┤ GND              │
└────────────┘    └──────────────────┘
     GT911
```

## 3. 初期設定

GT911の初期設定では、以下のレジスタ値が設定されます：

### 3.1 レジスタ初期値

| レジスタ | 値 | 説明 |
|---------|------|------|
| 0x8048-0x8049 | 960 | X解像度 (M5Paper S3の画面解像度) |
| 0x804A-0x804B | 540 | Y解像度 (M5Paper S3の画面解像度) |
| 0x804C | 2 | 最大タッチポイント数 |
| 0x804D | 0x00 | モジュールスイッチ1 (反転なし、軸入れ替えなし) |
| 0x804E | 0x01 | モジュールスイッチ2 (タッチキー有効) |
| 0x8053 | 50 | タッチしきい値 |
| 0x8054 | 30 | リリースしきい値 |
| 0x8056 | 5 | レポートレート (10ms) |

### 3.2 タッチキー設定

| レジスタ | 値 | 説明 |
|---------|------|------|
| 0x8093 | 10 | キー1のアドレス |
| 0x8094 | 20 | キー2のアドレス |
| 0x8095 | 0 | キー3のアドレス (無効) |
| 0x8096 | 0 | キー4のアドレス (無効) |
| 0x8098 | 40 | キータッチしきい値 |
| 0x8099 | 25 | キーリリースしきい値 |

## 4. ソフトウェア実装

### 4.1 コード構成

- **gt911.h**: GT911タッチコントローラのヘッダファイル
- **gt911.c**: GT911ドライバの実装
- **epd_main.c**: メインアプリケーションおよび初期化コード

### 4.2 初期化シーケンス

```c
// GT911初期化シーケンス
GT911_Device gt911_dev = {0};

// 1. デバイス構造体の初期化
bool status = gt911_init(&gt911_dev, 
                         GPIO_NUM_41,  // SDA
                         GPIO_NUM_42,  // SCL
                         GPIO_NUM_48,  // INT
                         -1);          // RST (外部プルアップ使用時は-1)

if (!status) {
    ESP_LOGE(TAG, "GT911 initialization failed");
    return;
}

// 2. タッチ感度の設定
gt911_set_sensitivity(&gt911_dev, 50, 30);

// 3. タッチキーの設定
gt911_configure_touch_keys(&gt911_dev, true);
gt911_set_touch_key(&gt911_dev, 1, 10, 8);  // キー1設定
gt911_set_touch_key(&gt911_dev, 2, 20, 8);  // キー2設定

// 4. コールバック関数の登録
gt911_register_callback(&gt911_dev, touch_event_callback, NULL);
```

## 5. 関数仕様

### 5.1 初期化関数

```c
bool gt911_init(GT911_Device* device, gpio_num_t sda_pin, gpio_num_t scl_pin, 
                gpio_num_t int_pin, gpio_num_t rst_pin);
```

**機能**: GT911タッチコントローラを初期化します。

**引数**:
- `device`: GT911デバイス構造体へのポインタ
- `sda_pin`: I²C SDAピン番号
- `scl_pin`: I²C SCLピン番号
- `int_pin`: 割り込みピン番号
- `rst_pin`: リセットピン番号（不要な場合は-1）

**戻り値**: 初期化成功の場合true、失敗の場合false

**処理**:
1. I²Cドライバの初期化
2. GPIOピンの設定
3. デバイスのハードウェアリセット
4. 製品IDの確認
5. 初期設定の送信
6. 割り込みハンドラの設定

### 5.2 タッチデータ読み取り関数

```c
bool gt911_read_touch_data(GT911_Device* device);
```

**機能**: タッチパネルの現在の状態を読み取ります。

**引数**:
- `device`: GT911デバイス構造体へのポインタ

**戻り値**: 読み取り成功の場合true、失敗の場合false

**処理**:
1. ステータスレジスタの読み取り
2. タッチフラグの確認
3. アクティブなタッチポイント数の取得
4. 各タッチポイントの座標データの読み取り
5. ステータスレジスタのクリア

### 5.3 タッチコールバック登録関数

```c
void gt911_register_callback(GT911_Device* device, gt911_touch_callback_t callback, void* user_data);
```

**機能**: タッチイベント発生時に呼び出されるコールバック関数を登録します。

**引数**:
- `device`: GT911デバイス構造体へのポインタ
- `callback`: コールバック関数ポインタ
- `user_data`: コールバック関数に渡されるユーザーデータ

**処理**:
1. デバイス構造体にコールバック関数を登録
2. ユーザーデータポインタの保存

### 5.4 タッチ感度設定関数

```c
bool gt911_set_sensitivity(GT911_Device* device, uint8_t touch_threshold, uint8_t leave_threshold);
```

**機能**: タッチパネルの感度を設定します。

**引数**:
- `device`: GT911デバイス構造体へのポインタ
- `touch_threshold`: タッチ検出しきい値 (0-255)
- `leave_threshold`: タッチリリースしきい値 (0-255)

**戻り値**: 設定成功の場合true、失敗の場合false

**処理**:
1. タッチしきい値レジスタ(0x8053)への書き込み
2. リリースしきい値レジスタ(0x8054)への書き込み
3. 設定の反映確認

### 5.5 タッチキー設定関数

```c
bool gt911_configure_touch_keys(GT911_Device* device, bool enable);
```

**機能**: タッチキー機能の有効/無効を設定します。

**引数**:
- `device`: GT911デバイス構造体へのポインタ
- `enable`: キー機能を有効にするかどうか

**戻り値**: 設定成功の場合true、失敗の場合false

**処理**:
1. モジュールスイッチ2レジスタ(0x804E)の読み取り
2. タッチキービットの設定/クリア
3. レジスタへの書き込み

```c
bool gt911_set_touch_key(GT911_Device* device, uint8_t key_id, uint8_t address, uint8_t sensitivity);
```

**機能**: 特定のタッチキーを設定します。

**引数**:
- `device`: GT911デバイス構造体へのポインタ
- `key_id`: キーID (1-4)
- `address`: キーアドレス (0-255, 0は無効)
- `sensitivity`: キーの感度 (0-15)

**戻り値**: 設定成功の場合true、失敗の場合false

**処理**:
1. キーアドレスレジスタ(0x8093+key_id-1)への書き込み
2. キー感度レジスタの設定

### 5.6 スリープモード制御関数

```c
bool gt911_enter_sleep_mode(GT911_Device* device);
```

**機能**: GT911をスリープモードに切り替えます。

**引数**:
- `device`: GT911デバイス構造体へのポインタ

**戻り値**: 設定成功の場合true、失敗の場合false

**処理**:
1. コマンドレジスタ(0x8040)に0x05を書き込み

```c
bool gt911_exit_sleep_mode(GT911_Device* device);
```

**機能**: GT911をスリープモードから復帰させます。

**引数**:
- `device`: GT911デバイス構造体へのポインタ

**戻り値**: 設定成功の場合true、失敗の場合false

**処理**:
1. デバイスのハードウェアリセット実行

## 6. 割り込み処理

### 6.1 割り込みハンドラ

GT911からの割り込み信号（INTピン）は、GPIO割り込みとして設定します。

```c
// 割り込みハンドラの設定
gpio_config_t io_conf = {
    .pin_bit_mask = (1ULL << device->int_pin),
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .intr_type = GPIO_INTR_NEGEDGE, // 立下りエッジで割り込み
};
gpio_config(&io_conf);

// 割り込みハンドラの登録
gpio_install_isr_service(0);
gpio_isr_handler_add(device->int_pin, gt911_interrupt_handler, device);
```

### 6.2 割り込み処理タスク

割り込み発生時の処理はタスクとして実行されます。

```c
// 割り込みハンドラ（ISR内）
static void IRAM_ATTR gt911_interrupt_handler(void* arg) {
    GT911_Device* device = (GT911_Device*)arg;
    // セマフォを通知
    xSemaphoreGiveFromISR(device->touch_semaphore, NULL);
}

// 割り込み処理タスク
static void gt911_task(void* pvParameters) {
    GT911_Device* device = (GT911_Device*)pvParameters;
    
    while(1) {
        // 割り込み待機
        if (xSemaphoreTake(device->touch_semaphore, portMAX_DELAY) == pdTRUE) {
            // タッチデータ読み取り
            if (gt911_read_touch_data(device)) {
                // コールバック関数呼び出し
                if (device->callback) {
                    device->callback(device, device->user_data);
                }
            }
        }
    }
}
```

## 7. アプリケーション例

### 7.1 タッチイベント処理

```c
// タッチイベントコールバック関数
void touch_event_callback(GT911_Device* device, void* user_data) {
    if (device->active_points > 0) {
        // タッチ検出
        for (int i = 0; i < device->active_points; i++) {
            printf("Touch point %d: x=%d, y=%d, size=%d\n", 
                  i, device->points[i].x, device->points[i].y, device->points[i].size);
            
            // 画面上のタッチ位置に対する処理
            handle_touch_position(device->points[i].x, device->points[i].y);
        }
    }
    
    // タッチキー処理
    for (int i = 0; i < 4; i++) {
        if (device->keys[i].is_pressed) {
            printf("Key %d pressed\n", device->keys[i].key_id);
            handle_key_press(device->keys[i].key_id);
        }
    }
}
```

### 7.2 省電力制御

```c
// スリープモードへの移行
void enter_low_power_mode() {
    // タッチパネルをスリープモードに設定
    gt911_enter_sleep_mode(&gt911_dev);
    
    // その他の省電力処理
    // ...
}

// スリープモードからの復帰
void exit_low_power_mode() {
    // タッチパネルをスリープから復帰
    gt911_exit_sleep_mode(&gt911_dev);
    
    // その他の復帰処理
    // ...
}
```

## 8. デバッグ手順

### 8.1 製品ID確認

初期化時にGT911の製品IDを確認することで、通信が正常に行われているかを検証できます。

```c
uint8_t product_id[4];
if (gt911_get_product_id(&gt911_dev, product_id)) {
    printf("GT911 Product ID: %02X%02X%02X%02X\n", 
           product_id[0], product_id[1], product_id[2], product_id[3]);
} else {
    printf("Failed to read product ID\n");
}
```

### 8.2 I²C通信デバッグ

I²C通信の問題を診断するためのログ出力：

```c
ESP_LOGI(TAG, "Writing to register 0x%04X", reg);
esp_err_t ret = i2c_master_write_to_device(device->i2c_port, device->i2c_addr, 
                                          buffer, len, GT911_I2C_TIMEOUT_MS);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
    return false;
}
```

### 8.3 タッチポイント検証

タッチポイントが正しく検出されるか検証するためのテストコードです：

```c
// タッチポイント検証用描画関数
void draw_touch_point(int x, int y, uint8_t color) {
    // EPDに円を描画
    epd_wrapper_fill_circle(epd, x, y, 10, color);
    epd_wrapper_update_screen(epd, MODE_DU);
}

// タッチ検証タスク
void touch_test_task(void* pvParameters) {
    while (1) {
        // タッチデータ読み取り
        if (gt911_read_touch_data(&gt911_dev)) {
            // タッチポイントが存在する場合
            if (gt911_dev.active_points > 0) {
                for (int i = 0; i < gt911_dev.active_points; i++) {
                    // タッチポイントに円を描画
                    draw_touch_point(gt911_dev.points[i].x, gt911_dev.points[i].y, 0x00);
                    
                    // タッチ情報をログ出力
                    ESP_LOGI(TAG, "Touch %d: x=%d, y=%d, size=%d", 
                             i, gt911_dev.points[i].x, gt911_dev.points[i].y, 
                             gt911_dev.points[i].size);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20)); // 50Hzのサンプリングレート
    }
}
```

## 9. 性能とリソース使用量

### 9.1 タスク優先度と消費メモリ

| 項目 | 値 | 説明 |
|-----|------|------|
| 割り込み処理タスク優先度 | 10 | 中程度の優先度（タッチ応答性重視） |
| 割り込み処理タスクスタックサイズ | 2048 bytes | タッチデータ処理に十分なスタック |
| I²C通信タイムアウト | 100 ms | I²C通信の最大待機時間 |
| GT911デバイス構造体サイズ | 約128 bytes | デバイス状態管理用メモリ |

### 9.2 タイミング特性

| 操作 | 所要時間 | 説明 |
|-----|---------|------|
| 初期化時間 | 約300 ms | GT911の初期化とI²C設定 |
| タッチ読み取り時間 | 約2 ms | 1回のタッチデータ読み取り |
| 割り込み反応時間 | 約5 ms | 割り込み検出からコールバック実行まで |
| スリープ移行時間 | 約20 ms | スリープモードへの移行時間 |
| スリープ復帰時間 | 約300 ms | スリープからの復帰時間（リセット含む） |

## 10. 既知の制限事項と回避策

### 10.1 既知の問題

1. **割り込み誤検出**:
   - 症状: 電源ノイズによる割り込み誤検出
   - 解決策: デバウンス処理の実装と適切なプルアップ抵抗の追加

2. **再起動後の通信失敗**:
   - 症状: デバイス再起動後にI²C通信が確立できない場合がある
   - 解決策: 初期化時のリセットシーケンスを確実に実行

3. **タッチ座標が反転**:
   - 症状: X/Y座標が反転している
   - 解決策: モジュールスイッチ1レジスタ(0x804D)のX/Y反転ビットを適切に設定

### 10.2 対応策

1. **タッチノイズ対策**:
   ```c
   // ノイズフィルタリング設定
   uint8_t filter_value = 3; // フィルタレベル
   gt911_write_registers(&gt911_dev, 0x8050, &filter_value, 1);
   ```

2. **リセット手順の強化**:
   ```c
   // 拡張リセット手順
   void gt911_enhanced_reset(GT911_Device* device) {
       // 通常のリセット実行
       gt911_reset(device);
       
       // 追加のリセット確認
       uint8_t status = 0;
       if (!gt911_read_registers(device, 0x814E, &status, 1)) {
           ESP_LOGW(TAG, "Reset verification failed, trying again");
           gt911_reset(device); // 再リセット
       }
   }
   ```

## 11. 拡張機能

### 11.1 ジェスチャーモード

GT911のジェスチャーモードを有効にして、スリープ中のウェイクアップジェスチャーを検出する機能：

```c
// ジェスチャーモード有効化
bool gt911_enable_gesture_mode(GT911_Device* device) {
    // コマンドレジスタに0x08を書き込み
    uint8_t cmd = 0x08;
    return gt911_write_registers(device, 0x8040, &cmd, 1);
}

// ジェスチャー検出
bool gt911_read_gesture(GT911_Device* device, uint8_t* gesture_id) {
    // ジェスチャーIDレジスタから読み取り
    return gt911_read_registers(device, 0x814C, gesture_id, 1);
}
```

### 11.2 自動キャリブレーション

GT911の自動キャリブレーションを実行する機能：

```c
// 自動キャリブレーション実行
bool gt911_perform_calibration(GT911_Device* device) {
    // コマンドレジスタに0x04を書き込み
    uint8_t cmd = 0x04;
    return gt911_write_registers(device, 0x8040, &cmd, 1);
}
```

## 12. 設定変更ガイドライン

### 12.1 画面解像度の変更

異なる解像度のディスプレイを使用する場合の設定変更手順：

```c
// 解像度設定関数
bool gt911_set_resolution(GT911_Device* device, uint16_t x_res, uint16_t y_res) {
    uint8_t config[4];
    
    // X解像度設定
    config[0] = x_res & 0xFF;        // 下位バイト
    config[1] = (x_res >> 8) & 0xFF; // 上位バイト
    
    // Y解像度設定
    config[2] = y_res & 0xFF;        // 下位バイト
    config[3] = (y_res >> 8) & 0xFF; // 上位バイト
    
    // レジスタに書き込み
    if (!gt911_write_registers(device, 0x8048, config, 4)) {
        return false;
    }
    
    // 設定の更新
    device->x_resolution = x_res;
    device->y_resolution = y_res;
    
    return true;
}
```

### 12.2 タッチ感度の最適化

異なる環境や用途に合わせたタッチ感度の最適化ガイドライン：

| 環境/用途 | タッチしきい値 | リリースしきい値 | 備考 |
|---------|------------|--------------|------|
| 標準使用 | 50 | 30 | 一般的な使用に適した設定 |
| 手袋使用 | 30 | 15 | 手袋着用時の感度向上 |
| 高精度操作 | 70 | 45 | 誤動作防止と精密操作向け |
| 防水環境 | 40 | 25 | 水滴による誤検出防止 |

タッチ感度設定例：
```c
// 手袋使用モード設定
gt911_set_sensitivity(&gt911_dev, 30, 15);

// 高精度操作モード設定
gt911_set_sensitivity(&gt911_dev, 70, 45);
```

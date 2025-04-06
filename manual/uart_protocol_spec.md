# M5Paper S3 UARTファイル転送プロトコル仕様書

## 1. 概要

本仕様書は、M5Paper S3デバイスとPC間でUART通信を使用したファイル転送プロトコルを定義します。このプロトコルを使用することで、SDカード上のファイルやディレクトリの操作が可能になります。

主な機能：
- ファイル/ディレクトリ一覧の取得
- ファイル転送（読み込み/書き込み）
- ディレクトリの作成/削除
- ファイルの削除
- デバイス状態の確認

## 2. UART通信仕様

### 2.1 基本設定

| 項目 | 設定値 |
|------|--------|
| ボーレート | 115200 bps |
| データビット | 8ビット |
| パリティ | なし |
| ストップビット | 1ビット |
| フロー制御 | なし |
| 通信バッファサイズ | 4096バイト |

### 2.2 パケット構造

プロトコルではバイナリパケット形式を採用しています。すべてのコマンドとレスポンスは以下の形式に従います：

```
[START_MARKER][COMMAND/RESPONSE][DATA_LENGTH_L][DATA_LENGTH_H][DATA...][CRC_L][CRC_H][END_MARKER]
```

| フィールド | サイズ | 説明 |
|------------|--------|------|
| START_MARKER | 1バイト | 0xAA (パケット開始マーカー) |
| COMMAND/RESPONSE | 1バイト | コマンドコードまたはレスポンスコード |
| DATA_LENGTH_L | 1バイト | データ長下位バイト |
| DATA_LENGTH_H | 1バイト | データ長上位バイト |
| DATA | 可変長 | コマンド/レスポンスのデータ |
| CRC_L | 1バイト | CRC-16下位バイト |
| CRC_H | 1バイト | CRC-16上位バイト |
| END_MARKER | 1バイト | 0x55 (パケット終了マーカー) |

### 2.3 CRC計算

CRC-16アルゴリズム（MODBUS多項式：0xA001）を使用します。CRC計算対象は「COMMAND/RESPONSE」フィールドから「DATA」フィールドまでです。

```c
uint16_t calculate_crc16(const uint8_t *data, int length) {
    uint16_t crc = 0xFFFF;
    
    for (int i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i];
        
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    
    return crc;
}
```

## 3. コマンド仕様

### 3.1 コマンドコード一覧

| コマンド | コード | 説明 |
|----------|--------|------|
| CMD_PING | 0x01 | デバイス状態確認 |
| CMD_RESET | 0x02 | デバイスリセット |
| CMD_FILE_LIST | 0x10 | ファイル/フォルダ一覧取得 |
| CMD_FILE_INFO | 0x11 | ファイル情報取得 |
| CMD_FILE_EXIST | 0x12 | ファイル存在確認 |
| CMD_FILE_OPEN | 0x20 | ファイル転送開始 |
| CMD_FILE_DATA | 0x21 | ファイルデータ転送 |
| CMD_FILE_CLOSE | 0x22 | ファイル転送終了 |
| CMD_FILE_DELETE | 0x30 | ファイル削除 |
| CMD_DIR_CREATE | 0x31 | フォルダ作成 |
| CMD_DIR_DELETE | 0x32 | フォルダ削除（再帰的） |

### 3.2 レスポンスコード一覧

| レスポンス | コード | 説明 |
|------------|--------|------|
| RESP_OK | 0xE0 | 成功 |
| RESP_ERROR | 0xE1 | 一般エラー |
| RESP_FILE_NOT_FOUND | 0xE2 | ファイルが見つからない |
| RESP_DISK_FULL | 0xE3 | ディスクフル |
| RESP_INVALID_PARAM | 0xE4 | 不正なパラメータ |

### 3.3 コマンド詳細

#### 3.3.1 デバイス状態確認（CMD_PING: 0x01）

**説明**: デバイスの現在の状態情報を取得します。

**リクエスト**:
- データなし

**レスポンス**:
- データ形式:
  ```c
  typedef struct {
      uint32_t heap_free;       // 空きヒープメモリ（バイト）
      uint8_t  sd_mounted;      // SDカードマウント状態（0=未マウント, 1=マウント済）
      uint64_t sd_total_space;  // SDカード総容量（バイト）
      uint64_t sd_free_space;   // SDカード空き容量（バイト）
      uint32_t uptime;          // 起動時間（秒）
  } device_status_t;
  ```

#### 3.3.2 デバイスリセット（CMD_RESET: 0x02）

**説明**: デバイスをリセットします。

**リクエスト**:
- データなし

**レスポンス**:
- 成功後、デバイスはリセットされるため、レスポンスはクライアント側で処理されないことがあります。

#### 3.3.3 ファイル/フォルダ一覧取得（CMD_FILE_LIST: 0x10）

**説明**: 指定されたディレクトリ内のファイルとサブディレクトリの一覧を取得します。

**リクエスト**:
- データ: 取得するディレクトリのパス（UTF-8文字列、NULL終端なし）
- 例: `/images` (SDカードのルートからの相対パス)

**レスポンス**:
- データ形式: ファイル情報の連続
  ```
  [Type][Size][Modified Time][Name Length][Name]...
  ```
  - Type: 1バイト (0=ファイル, 1=ディレクトリ)
  - Size: 4バイト (リトルエンディアン)
  - Modified Time: 4バイト (Unix timestamp、リトルエンディアン)
  - Name Length: 1バイト
  - Name: 可変長 (UTF-8文字列)

#### 3.3.4 ファイル情報取得（CMD_FILE_INFO: 0x11）

**説明**: 指定されたファイルまたはディレクトリの詳細情報を取得します。

**リクエスト**:
- データ: 情報を取得するファイル/ディレクトリのパス（UTF-8文字列、NULL終端なし）

**レスポンス**:
- データ形式:
  ```c
  typedef struct {
      uint8_t  type;      // 0=ファイル, 1=ディレクトリ
      uint32_t size;      // ファイルサイズ
      uint32_t created;   // 作成日時 (Unix timestamp)
      uint32_t modified;  // 更新日時 (Unix timestamp)
  } file_info_t;
  ```

#### 3.3.5 ファイル存在確認（CMD_FILE_EXIST: 0x12）

**説明**: 指定されたパスのファイルまたはディレクトリが存在するかを確認します。

**リクエスト**:
- データ: 確認するファイル/ディレクトリのパス（UTF-8文字列、NULL終端なし）

**レスポンス**:
- データ形式: [存在フラグ][タイプ]
  - 存在フラグ: 1バイト (0=存在しない, 1=存在する)
  - タイプ: 1バイト (0=ファイル, 1=ディレクトリ)

#### 3.3.6 ファイル転送開始（CMD_FILE_OPEN: 0x20）

**説明**: ファイル転送セッションを開始します。

**リクエスト**:
- データ形式: [モード][ファイルパス]
  - モード: 1バイト (0=読込, 1=書込, 2=追記)
  - ファイルパス: 可変長 (UTF-8文字列、NULL終端なし)

**レスポンス**:
- 成功時はデータなし (RESP_OK)
- 失敗時はエラーコード

#### 3.3.7 ファイルデータ転送（CMD_FILE_DATA: 0x21）

**説明**: ファイルデータの読み書きを行います。

**リクエスト (読込モード)**:
- データ形式: [方向][読込サイズL][読込サイズH]
  - 方向: 1バイト (0=読込)
  - 読込サイズ: 2バイト (リトルエンディアン、読み込む最大バイト数)

**リクエスト (書込モード)**:
- データ形式: [方向][データ...]
  - 方向: 1バイト (1=書込)
  - データ: 可変長 (書き込むバイナリデータ)

**レスポンス (読込モード)**:
- データ形式: [EOFフラグ][データ...]
  - EOFフラグ: 1バイト (0=続きあり, 1=ファイル終端)
  - データ: 可変長 (読み込んだバイナリデータ)

**レスポンス (書込モード)**:
- 成功時はデータなし (RESP_OK)
- 失敗時はエラーコード

#### 3.3.8 ファイル転送終了（CMD_FILE_CLOSE: 0x22）

**説明**: ファイル転送セッションを終了します。

**リクエスト**:
- データなし

**レスポンス**:
- 成功時はデータなし (RESP_OK)
- 失敗時はエラーコード

#### 3.3.9 ファイル削除（CMD_FILE_DELETE: 0x30）

**説明**: 指定されたファイルを削除します。

**リクエスト**:
- データ: 削除するファイルのパス（UTF-8文字列、NULL終端なし）

**レスポンス**:
- 成功時はデータなし (RESP_OK)
- 失敗時はエラーコード

#### 3.3.10 フォルダ作成（CMD_DIR_CREATE: 0x31）

**説明**: 指定されたパスにディレクトリを作成します。

**リクエスト**:
- データ: 作成するディレクトリのパス（UTF-8文字列、NULL終端なし）

**レスポンス**:
- 成功時はデータなし (RESP_OK)
- 失敗時はエラーコード

#### 3.3.11 フォルダ削除（CMD_DIR_DELETE: 0x32）

**説明**: 指定されたディレクトリを再帰的に削除します（空でないディレクトリも削除可能）。

**リクエスト**:
- データ: 削除するディレクトリのパス（UTF-8文字列、NULL終端なし）

**レスポンス**:
- 成功時はデータなし (RESP_OK)
- 失敗時はエラーコード

## 4. M5Paper S3 実装ガイド

### 4.1 必要なコンポーネント

M5Paper S3側の実装には以下のモジュールが含まれます：

1. **protocol.h**: プロトコル定義（コマンド、レスポンスコードなど）
2. **sdcard_manager.c/h**: SDカード制御モジュール
3. **uart_command.c/h**: UART通信モジュール
4. **file_transfer.c/h**: ファイル転送モジュール
5. **command_handlers.c/h**: コマンドハンドラモジュール

### 4.2 モジュール間の関係

```
+------------------+      +------------------+
| command_handlers |----->| file_transfer    |
+------------------+      +------------------+
        |                          |
        v                          v
+------------------+      +------------------+
| uart_command     |      | sdcard_manager   |
+------------------+      +------------------+
        |                          |
        v                          v
+--------------------------------------------+
|                protocol.h                  |
+--------------------------------------------+
```

### 4.3 初期化シーケンス

```c
// メイン関数でのモジュール初期化

// 1. SDカード初期化
if (sdcard_init() != ESP_OK) {
    ESP_LOGW(TAG, "SDカードの初期化に失敗しました。一部機能が制限されます。");
}

// 2. ファイル転送モジュール初期化
file_transfer_init();

// 3. コマンドハンドラ初期化
command_handlers_init();

// 4. UARTコマンドモジュール初期化
if (!uart_command_init()) {
    ESP_LOGE(TAG, "UART通信の初期化に失敗しました");
    return;
}

// 5. コマンドハンドラを登録
uart_register_command_handler(command_handler_process);

// 6. UARTタスク開始
uart_command_start();
```

### 4.4 メモリ使用量の考慮事項

1. **バッファサイズ**:
   - `MAX_PATH_LENGTH`: 256バイト (パス名最大長)
   - `UART_BUF_SIZE`: 4096バイト (UART受信バッファ)
   - `PACKET_BUF_SIZE`: 8192バイト (処理用バッファ)

2. **タスクスタックサイズ**:
   - UART受信タスク: 4096バイト (推奨)

3. **プリオリティ**:
   - UART受信タスク: 5 (中程度の優先度)

### 4.5 エラー処理

各モジュールはエラーを適切にログ出力し、エラーコードまたはブール値を返します。すべてのエラーは適切に処理され、クライアントに通知されるべきです。

## 5. クライアント側実装ガイド

### 5.1 基本フロー

1. UARTポートを開く
2. コマンドパケットを作成
3. CRCを計算
4. パケットを送信
5. レスポンスを受信
6. レスポンスを検証（マーカー、CRC）
7. レスポンスデータを処理

### 5.2 サンプルコード（Python）

```python
import serial
import struct
import time

# 定数定義
START_MARKER = 0xAA
END_MARKER = 0x55

# コマンドコード
CMD_PING = 0x01
CMD_RESET = 0x02
CMD_FILE_LIST = 0x10
CMD_FILE_INFO = 0x11
CMD_FILE_EXIST = 0x12
CMD_FILE_OPEN = 0x20
CMD_FILE_DATA = 0x21
CMD_FILE_CLOSE = 0x22
CMD_FILE_DELETE = 0x30
CMD_DIR_CREATE = 0x31
CMD_DIR_DELETE = 0x32

# レスポンスコード
RESP_OK = 0xE0
RESP_ERROR = 0xE1
RESP_FILE_NOT_FOUND = 0xE2
RESP_DISK_FULL = 0xE3
RESP_INVALID_PARAM = 0xE4

class M5PaperClient:
    def __init__(self, port, baudrate=115200):
        self.serial = serial.Serial(port, baudrate, timeout=1)
        self.serial.reset_input_buffer()
        
    def __del__(self):
        if hasattr(self, 'serial') and self.serial.is_open:
            self.serial.close()
    
    def calculate_crc16(self, data):
        crc = 0xFFFF
        for b in data:
            crc ^= b
            for _ in range(8):
                if crc & 1:
                    crc = (crc >> 1) ^ 0xA001
                else:
                    crc >>= 1
        return crc
    
    def send_command(self, command, data=b''):
        # データ長を計算
        data_length = len(data)
        
        # CRC計算用のデータ
        crc_data = bytes([command]) + data
        crc = self.calculate_crc16(crc_data)
        
        # パケット作成
        packet = bytes([
            START_MARKER,
            command,
            data_length & 0xFF,
            (data_length >> 8) & 0xFF
        ]) + data + bytes([
            crc & 0xFF,
            (crc >> 8) & 0xFF,
            END_MARKER
        ])
        
        # 送信
        self.serial.write(packet)
        
        # レスポンス受信
        state = 0  # 0=マーカー待ち, 1=コード, 2=長さL, 3=長さH, 4=データ, 5=CRCL, 6=CRCH, 7=終了マーカー
        response_code = 0
        data_length = 0
        response_data = bytearray()
        crc = 0
        
        timeout = time.time() + 5.0  # 5秒タイムアウト
        
        while time.time() < timeout:
            if self.serial.in_waiting > 0:
                b = ord(self.serial.read(1))
                
                if state == 0:  # マーカー待ち
                    if b == START_MARKER:
                        state = 1
                
                elif state == 1:  # レスポンスコード
                    response_code = b
                    state = 2
                
                elif state == 2:  # データ長L
                    data_length = b
                    state = 3
                
                elif state == 3:  # データ長H
                    data_length |= (b << 8)
                    if data_length > 0:
                        state = 4
                    else:
                        state = 5
                
                elif state == 4:  # データ
                    response_data.append(b)
                    if len(response_data) >= data_length:
                        state = 5
                
                elif state == 5:  # CRC L
                    crc = b
                    state = 6
                
                elif state == 6:  # CRC H
                    crc |= (b << 8)
                    state = 7
                
                elif state == 7:  # 終了マーカー
                    if b == END_MARKER:
                        # CRC検証
                        calc_crc = self.calculate_crc16(bytes([response_code]) + response_data)
                        if calc_crc == crc:
                            return response_code, bytes(response_data)
                        else:
                            raise Exception("CRC不一致")
                    break
            
            time.sleep(0.01)
        
        raise Exception("タイムアウトまたは不正なパケット")
    
    # 以下、具体的なコマンドの実装
    
    def ping(self):
        """デバイス状態確認"""
        resp_code, data = self.send_command(CMD_PING)
        if resp_code == RESP_OK and len(data) >= 17:
            heap_free = struct.unpack('<I', data[0:4])[0]
            sd_mounted = data[4] > 0
            sd_total = struct.unpack('<Q', data[5:13])[0]
            sd_free = struct.unpack('<Q', data[13:21])[0]
            uptime = struct.unpack('<I', data[21:25])[0] if len(data) >= 25 else 0
            
            return {
                'heap_free': heap_free,
                'sd_mounted': sd_mounted,
                'sd_total': sd_total,
                'sd_free': sd_free,
                'uptime': uptime
            }
        else:
            raise Exception(f"PING失敗: {resp_code}")
    
    def reset(self):
        """デバイスリセット"""
        try:
            resp_code, _ = self.send_command(CMD_RESET)
            return resp_code == RESP_OK
        except:
            # リセット後に応答が来ない場合もあるのでTrue扱い
            return True
    
    def get_file_list(self, path):
        """ファイル/フォルダ一覧取得"""
        resp_code, data = self.send_command(CMD_FILE_LIST, path.encode('utf-8'))
        if resp_code == RESP_OK:
            files = []
            pos = 0
            while pos < len(data):
                if pos + 10 > len(data):
                    break
                
                file_type = data[pos]
                file_size = struct.unpack('<I', data[pos+1:pos+5])[0]
                modified_time = struct.unpack('<I', data[pos+5:pos+9])[0]
                name_len = data[pos+9]
                
                if pos + 10 + name_len > len(data):
                    break
                
                name = data[pos+10:pos+10+name_len].decode('utf-8')
                
                files.append({
                    'name': name,
                    'type': 'directory' if file_type else 'file',
                    'size': file_size,
                    'modified': modified_time
                })
                
                pos += 10 + name_len
            
            return files
        else:
            raise Exception(f"ファイル一覧取得失敗: {resp_code}")
    
    def upload_file(self, src_path, dst_path, callback=None):
        """ファイルアップロード"""
        # ファイルオープン
        cmd_data = bytes([1]) + dst_path.encode('utf-8')  # モード1=書込
        resp_code, _ = self.send_command(CMD_FILE_OPEN, cmd_data)
        if resp_code != RESP_OK:
            raise Exception(f"ファイルオープン失敗: {resp_code}")
        
        try:
            # ソースファイルを読み込んで転送
            with open(src_path, 'rb') as f:
                total_size = 0
                chunk_size = 1024  # 一度に転送するサイズ
                
                while True:
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break
                    
                    # データ転送
                    cmd_data = bytes([1]) + chunk  # モード1=書込
                    resp_code, _ = self.send_command(CMD_FILE_DATA, cmd_data)
                    if resp_code != RESP_OK:
                        raise Exception(f"ファイル書き込み失敗: {resp_code}")
                    
                    total_size += len(chunk)
                    
                    # 進捗コールバック
                    if callback:
                        callback(total_size)
            
            # ファイルクローズ
            resp_code, _ = self.send_command(CMD_FILE_CLOSE)
            if resp_code != RESP_OK:
                raise Exception(f"ファイルクローズ失敗: {resp_code}")
            
            return True
            
        except Exception as e:
            # エラー時はファイルを閉じる
            try:
                self.send_command(CMD_FILE_CLOSE)
            except:
                pass
            raise e
    
    def download_file(self, src_path, dst_path, callback=None):
        """ファイルダウンロード"""
        # ファイルオープン
        cmd_data = bytes([0]) + src_path.encode('utf-8')  # モード0=読込
        resp_code, _ = self.send_command(CMD_FILE_OPEN, cmd_data)
        if resp_code != RESP_OK:
            raise Exception(f"ファイルオープン失敗: {resp_code}")
        
        try:
            # 出力ファイルを開く
            with open(dst_path, 'wb') as f:
                total_size = 0
                chunk_size = 1024  # 一度に要求するサイズ
                eof = False
                
                while not eof:
                    # データ要求
                    cmd_data = struct.pack('<BH', 0, chunk_size)  # モード0=読込, サイズ
                    resp_code, data = self.send_command(CMD_FILE_DATA, cmd_data)
                    if resp_code != RESP_OK:
                        raise Exception(f"ファイル読み込み失敗: {resp_code}")
                    
                    # EOFフラグとデータを分離
                    eof = data[0] > 0
                    chunk = data[1:]
                    
                    # ファイルに書き込み
                    f.write(chunk)
                    
                    total_size += len(chunk)
                    
                    # 進捗コールバック
                    if callback:
                        callback(total_size)
            
            # ファイルクローズ
            resp_code, _ = self.send_command(CMD_FILE_CLOSE)
            if resp_code != RESP_OK:
                raise Exception(f"ファイルクローズ失敗: {resp_code}")
            
            return True
            
        except Exception as e:
            # エラー時はファイルを閉じる
            try:
                self.send_command(CMD_FILE_CLOSE)
            except:
                pass
            raise e
    
    def make_directory(self, path):
        """ディレクトリ作成"""
        resp_code, _ = self.send_command(CMD_DIR_CREATE, path.encode('utf-8'))
        return resp_code == RESP_OK
    
    def delete_file(self, path):
        """ファイル削除"""
        resp_code, _ = self.send_command(CMD_FILE_DELETE, path.encode('utf-8'))
        return resp_code == RESP_OK
    
    def delete_directory(self, path):
        """ディレクトリ削除（再帰的）"""
        resp_code, _ = self.send_command(CMD_DIR_DELETE, path.encode('utf-8'))
        return resp_code == RESP_OK

# 使用例
if __name__ == "__main__":
    # COM3ポートに接続 (Windowsの場合)
    client = M5PaperClient("COM3")
    
    # デバイス状態確認
    status = client.ping()
    print(f"デバイス状態: {status}")
    
    # ルートディレクトリ一覧
    files = client.get_file_list("/")
    print("ファイル一覧:")
    for file in files:
        print(f"  {'[DIR]' if file['type'] == 'directory' else '[FILE]'} {file['name']} ({file['size']} bytes)")
    
    # ファイルアップロード
    print("ファイルアップロード中...")
    client.upload_file("local_file.txt", "/uploaded.txt")
    print("アップロード完了")
    
    # ファイルダウンロード
    print("ファイルダウンロード中...")
    client.download_file("/existing_file.txt", "downloaded.txt")
    print("ダウンロード完了")
```

### 5.3 転送効率の考慮事項

1. **チャンクサイズ**:
   - 大きなファイル転送では、適切なチャンクサイズを選択することで効率が向上します
   - 推奨: 1024〜4096バイト

2. **エラー処理**:
   - 通信エラーや不完全なパケットに対処するための再試行メカニズムを実装する
   - タイムアウトを適切に設定する（大きなファイルでは長め、小さなコマンドでは短めに）

3. **プログレス表示**:
   - 大きなファイル転送では進捗状況の表示を実装することを推奨
   - バイト単位ではなくパーセンテージで表示するとユーザーフレンドリー

## 6. 応用例

### 6.1 ファイルブラウザアプリケーション

以下のような機能を含むシンプルなファイルブラウザを実装できます：

- SDカードのディレクトリツリー表示
- ファイルのアップロード/ダウンロード
- ディレクトリの作成/削除
- ファイルの削除
- ファイル情報の表示

```python
import tkinter as tk
from tkinter import ttk, filedialog
import os
from m5paper_client import M5PaperClient

class M5PaperFileBrowser:
    def __init__(self, master):
        self.master = master
        self.master.title("M5Paper File Browser")
        self.master.geometry("800x600")
        
        self.client = M5PaperClient("COM3")  # COMポートを適切に設定
        
        self.setup_ui()
        self.refresh_file_list()
    
    def setup_ui(self):
        # 上部フレーム（接続情報、ボタン）
        top_frame = ttk.Frame(self.master)
        top_frame.pack(fill=tk.X, padx=5, pady=5)
        
        # 下部フレーム（ファイルリスト、操作ボタン）
        main_frame = ttk.Frame(self.master)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # 左側（ファイルリスト）
        list_frame = ttk.Frame(main_frame)
        list_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        
        # ファイルリスト
        self.file_tree = ttk.Treeview(list_frame, columns=("size", "type", "modified"))
        self.file_tree.heading("#0", text="Name")
        self.file_tree.heading("size", text="Size")
        self.file_tree.heading("type", text="Type")
        self.file_tree.heading("modified", text="Modified")
        self.file_tree.pack(fill=tk.BOTH, expand=True)
        
        # 右側（操作ボタン）
        button_frame = ttk.Frame(main_frame)
        button_frame.pack(side=tk.RIGHT, fill=tk.Y, padx=5)
        
        # ボタン
        ttk.Button(button_frame, text="Refresh", command=self.refresh_file_list).pack(fill=tk.X, pady=2)
        ttk.Button(button_frame, text="Upload", command=self.upload_file).pack(fill=tk.X, pady=2)
        ttk.Button(button_frame, text="Download", command=self.download_file).pack(fill=tk.X, pady=2)
        ttk.Button(button_frame, text="New Folder", command=self.create_folder).pack(fill=tk.X, pady=2)
        ttk.Button(button_frame, text="Delete", command=self.delete_item).pack(fill=tk.X, pady=2)
        
        # 現在のパス
        self.current_path = "/"
        self.path_var = tk.StringVar(value=self.current_path)
        ttk.Entry(top_frame, textvariable=self.path_var, width=50).pack(side=tk.LEFT, padx=5)
        ttk.Button(top_frame, text="Go", command=self.go_to_path).pack(side=tk.LEFT)
        
        # ステータスバー
        self.status_var = tk.StringVar()
        ttk.Label(self.master, textvariable=self.status_var, relief=tk.SUNKEN, anchor=tk.W).pack(fill=tk.X, side=tk.BOTTOM)
    
    def refresh_file_list(self):
        try:
            # ツリービューをクリア
            for item in self.file_tree.get_children():
                self.file_tree.delete(item)
            
            # ファイル一覧取得
            files = self.client.get_file_list(self.current_path)
            
            # ファイル一覧を表示
            for file in files:
                size_str = f"{file['size']:,} B" if file['type'] == 'file' else ""
                self.file_tree.insert("", "end", text=file['name'], 
                                    values=(size_str, file['type'], 
                                           self.format_timestamp(file['modified'])))
            
            self.status_var.set(f"Total items: {len(files)}")
        except Exception as e:
            self.status_var.set(f"Error: {str(e)}")
    
    def format_timestamp(self, timestamp):
        import datetime
        return datetime.datetime.fromtimestamp(timestamp).strftime('%Y-%m-%d %H:%M:%S')
    
    def go_to_path(self):
        path = self.path_var.get()
        if path:
            self.current_path = path
            self.refresh_file_list()
    
    def upload_file(self):
        filepath = filedialog.askopenfilename()
        if not filepath:
            return
        
        filename = os.path.basename(filepath)
        dest_path = os.path.join(self.current_path, filename).replace("\\", "/")
        
        try:
            self.status_var.set(f"Uploading {filename}...")
            self.master.update_idletasks()
            
            self.client.upload_file(filepath, dest_path, 
                                  lambda size: self.status_var.set(f"Uploaded {size:,} bytes..."))
            
            self.status_var.set(f"Upload complete: {filename}")
            self.refresh_file_list()
        except Exception as e:
            self.status_var.set(f"Upload error: {str(e)}")
    
    def download_file(self):
        selected = self.file_tree.selection()
        if not selected:
            return
        
        item = self.file_tree.item(selected[0])
        filename = item['text']
        file_type = item['values'][1]
        
        if file_type != 'file':
            self.status_var.set("Can only download files")
            return
        
        save_path = filedialog.asksaveasfilename(defaultextension=".*", 
                                              initialfile=filename)
        if not save_path:
            return
        
        source_path = os.path.join(self.current_path, filename).replace("\\", "/")
        
        try:
            self.status_var.set(f"Downloading {filename}...")
            self.master.update_idletasks()
            
            self.client.download_file(source_path, save_path, 
                                    lambda size: self.status_var.set(f"Downloaded {size:,} bytes..."))
            
            self.status_var.set(f"Download complete: {filename}")
        except Exception as e:
            self.status_var.set(f"Download error: {str(e)}")
    
    def create_folder(self):
        from tkinter import simpledialog
        folder_name = simpledialog.askstring("New Folder", "Enter folder name:")
        if not folder_name:
            return
        
        folder_path = os.path.join(self.current_path, folder_name).replace("\\", "/")
        
        try:
            if self.client.make_directory(folder_path):
                self.status_var.set(f"Created folder: {folder_name}")
                self.refresh_file_list()
            else:
                self.status_var.set("Failed to create folder")
        except Exception as e:
            self.status_var.set(f"Error: {str(e)}")
    
    def delete_item(self):
        selected = self.file_tree.selection()
        if not selected:
            return
        
        item = self.file_tree.item(selected[0])
        name = item['text']
        item_type = item['values'][1]
        
        if not tk.messagebox.askyesno("Confirm Delete", 
                                    f"Are you sure you want to delete {name}?"):
            return
        
        path = os.path.join(self.current_path, name).replace("\\", "/")
        
        try:
            if item_type == 'directory':
                if self.client.delete_directory(path):
                    self.status_var.set(f"Deleted directory: {name}")
                else:
                    self.status_var.set("Failed to delete directory")
            else:
                if self.client.delete_file(path):
                    self.status_var.set(f"Deleted file: {name}")
                else:
                    self.status_var.set("Failed to delete file")
            
            self.refresh_file_list()
        except Exception as e:
            self.status_var.set(f"Error: {str(e)}")

# アプリケーション起動
if __name__ == "__main__":
    root = tk.Tk()
    app = M5PaperFileBrowser(root)
    root.mainloop()
```

### 6.2 自動ファイル同期ツール

フォルダの同期を自動化するスクリプトの例：

```python
import os
import time
import hashlib
from m5paper_client import M5PaperClient

def get_file_hash(filepath):
    """ファイルのSHA256ハッシュを計算"""
    h = hashlib.sha256()
    with open(filepath, 'rb') as f:
        for chunk in iter(lambda: f.read(4096), b''):
            h.update(chunk)
    return h.hexdigest()

def sync_directory(client, local_dir, remote_dir):
    """ローカルディレクトリをM5Paper S3と同期"""
    print(f"同期開始: {local_dir} -> {remote_dir}")
    
    # リモートディレクトリが存在しない場合は作成
    try:
        client.get_file_list(remote_dir)
    except:
        print(f"リモートディレクトリを作成: {remote_dir}")
        client.make_directory(remote_dir)
    
    # ローカルファイル一覧を取得
    local_files = []
    for root, dirs, files in os.walk(local_dir):
        rel_path = os.path.relpath(root, local_dir)
        if rel_path == '.':
            rel_path = ''
        
        # サブディレクトリを処理
        for dir_name in dirs:
            local_subdir = os.path.join(root, dir_name)
            remote_subdir = os.path.join(remote_dir, rel_path, dir_name).replace('\\', '/')
            
            # リモートのサブディレクトリが存在しない場合は作成
            try:
                client.get_file_list(remote_subdir)
            except:
                print(f"リモートディレクトリを作成: {remote_subdir}")
                client.make_directory(remote_subdir)
        
        # ファイルを処理
        for file_name in files:
            local_filepath = os.path.join(root, file_name)
            rel_filepath = os.path.join(rel_path, file_name)
            remote_filepath = os.path.join(remote_dir, rel_filepath).replace('\\', '/')
            
            local_files.append({
                'path': rel_filepath,
                'full_path': local_filepath,
                'remote_path': remote_filepath,
                'size': os.path.getsize(local_filepath),
                'modified': os.path.getmtime(local_filepath)
            })
    
    # リモートファイル一覧を再帰的に取得する関数
    def get_remote_files(path, prefix=''):
        result = []
        
        try:
            files = client.get_file_list(path)
            
            for file in files:
                file_path = os.path.join(prefix, file['name']).replace('\\', '/')
                
                if file['type'] == 'directory':
                    # サブディレクトリを再帰的に処理
                    result.extend(get_remote_files(
                        os.path.join(path, file['name']).replace('\\', '/'),
                        file_path
                    ))
                else:
                    # ファイル情報を追加
                    result.append({
                        'path': file_path,
                        'remote_path': os.path.join(path, file['name']).replace('\\', '/'),
                        'size': file['size'],
                        'modified': file['modified']
                    })
        except Exception as e:
            print(f"リモートファイル一覧取得エラー: {path} - {str(e)}")
        
        return result
    
    # リモートファイル一覧を取得
    remote_files = get_remote_files(remote_dir)
    
    # ローカルファイルとリモートファイルのマップを作成
    local_file_map = {f['path']: f for f in local_files}
    remote_file_map = {f['path']: f for f in remote_files}
    
    # アップロードが必要なファイルを特定
    for path, local_file in local_file_map.items():
        if path not in remote_file_map:
            # リモートに存在しないファイル
            print(f"アップロード (新規): {path}")
            client.upload_file(local_file['full_path'], local_file['remote_path'])
        else:
            # 既存ファイルの比較
            remote_file = remote_file_map[path]
            
            # 更新日時かサイズが異なる場合、アップロード
            if local_file['size'] != remote_file['size'] or \
               abs(local_file['modified'] - remote_file['modified']) > 5:  # 5秒の誤差を許容
                print(f"アップロード (更新): {path}")
                client.upload_file(local_file['full_path'], local_file['remote_path'])
    
    # 削除が必要なファイルを特定（リモートにあってローカルにないもの）
    for path, remote_file in remote_file_map.items():
        if path not in local_file_map:
            print(f"削除: {path}")
            client.delete_file(remote_file['remote_path'])
    
    print("同期完了")

# 使用例
if __name__ == "__main__":
    client = M5PaperClient("COM3")
    
    # 同期先のディレクトリ
    local_directory = "C:/Projects/MyFiles"
    remote_directory = "/sync"
    
    while True:
        try:
            # フォルダを同期
            sync_directory(client, local_directory, remote_directory)
            print("5分後に再同期します...")
            time.sleep(300)  # 5分ごとに同期
        except KeyboardInterrupt:
            print("同期を終了します")
            break
        except Exception as e:
            print(f"エラー: {str(e)}")
            print("60秒後に再試行します...")
            time.sleep(60)  # エラー発生時は1分後に再試行
```

## 7. トラブルシューティング

### 7.1 通信エラー

| 症状 | 考えられる原因 | 対処法 |
|------|----------------|--------|
| レスポンスなし | 接続不良、ボーレート不一致 | ケーブル確認、ボーレート設定確認 |
| CRC不一致 | ノイズによるデータ破損 | パケット再送、ケーブル品質確認 |
| 通信タイムアウト | 処理時間超過、バッファオーバーフロー | タイムアウト値調整、小さなチャンクで転送 |

### 7.2 ファイル操作エラー

| エラーコード | 原因 | 対処法 |
|--------------|------|--------|
| RESP_FILE_NOT_FOUND | 存在しないファイル/パス | パスの正しさを確認、ディレクトリを作成 |
| RESP_DISK_FULL | SDカード容量不足 | 不要なファイルを削除 |
| RESP_ERROR | その他の一般エラー | ログを確認、操作をリトライ |

### 7.3 パフォーマンスの最適化

1. **転送速度向上**:
   - UARTボーレートを上げる（230400や460800など）
   - 大きなチャンクサイズを使用（2048〜4096バイト）

2. **メモリ使用量の最適化**:
   - 大きなファイルはストリーミング処理
   - 必要に応じてバッファサイズを調整

3. **エラー耐性向上**:
   - 自動再試行メカニズムの実装
   - ECCやより強固なCRCの導入

## 8. 将来の拡張可能性

### 8.1 プロトコル拡張

1. **ファイル圧縮**:
   - 転送前にデータを圧縮して効率化

2. **マルチパートダウンロード**:
   - 大きなファイルを並列チャンクで転送

3. **ファイルハッシュ検証**:
   - ファイル整合性チェック用のSHA-256など

### 8.2 代替通信方式

将来的には以下の方法も検討可能：

1. **USB MSC (Mass Storage Class)**:
   - USB経由でSDカードをマスストレージとして認識
   - 高速なファイル転送が可能

2. **Wi-Fi転送**:
   - M5Paper S3のWi-Fi機能を使用
   - WebインターフェースやFTP経由の転送

3. **BLE (Bluetooth Low Energy)**:
   - 低消費電力での無線転送
   - モバイルデバイスとの連携
# ESP32 UART File Transfer Test Tool

M5Paper S3デバイスなどのESP32ボードとPC間のUART通信によるファイル転送機能をテストするためのツールです。

## 機能概要

- デバイス状態確認（PING）
- ファイル一覧取得
- ファイル転送（アップロード/ダウンロード）
- ディレクトリ操作（作成/削除）
- ファイル操作（削除、情報取得）

## 環境要件

- Python 3.6以上
- pyserial 3.5以上
- （GUIモード用）tkinter

## インストール方法

1. リポジトリをクローンまたはダウンロードします
2. 必要なライブラリをインストールします：

```bash
pip install -r requirements.txt
```

または直接pyserialをインストールします：

```bash
pip install pyserial
```

## ファイル構成

- `uart_client.py` - UARTプロトコル通信の基本実装
- `main.py` - メインエントリーポイント
- `cli.py` - コマンドラインインターフェース
- `gui.py` - グラフィカルユーザーインターフェース
- `requirements.txt` - 必要なパッケージ

## 使用方法

### コマンドラインモード

基本的な使い方：

```bash
python main.py --port COM3
```

オプション：
- `--port` - シリアルポート（例：COM3、/dev/ttyUSB0）
- `--baudrate` - ボーレート（デフォルト：115200）
- `--debug` - デバッグモード有効
- `--gui` - GUIモード有効

### 対話型コマンドライン

コマンドラインモードでは、以下のコマンドが使用できます：

- `help` - ヘルプメッセージを表示
- `connect [PORT] [BAUDRATE]` - デバイスに接続
- `disconnect` - デバイスから切断
- `ping` - デバイス状態を確認
- `reset` - デバイスをリセット
- `ls [PATH]` - ディレクトリ内のファイル一覧を表示
- `info <PATH>` - ファイル情報を表示
- `exists <PATH>` - ファイル/ディレクトリの存在を確認
- `upload <LOCAL_PATH> <REMOTE_PATH>` - ファイルをアップロード
- `download <REMOTE_PATH> <LOCAL_PATH>` - ファイルをダウンロード
- `rm <PATH>` - ファイルを削除
- `mkdir <PATH>` - ディレクトリを作成
- `rmdir <PATH>` - ディレクトリを削除
- `exit/quit/q` - プログラムを終了

使用例：

```
> connect COM3
> ping
> ls /
> mkdir /test
> upload data.txt /test/data.txt
> ls /test
> download /test/data.txt downloaded.txt
> rm /test/data.txt
> rmdir /test
> quit
```

### GUIモード

GUIインターフェースを使用するには：

```bash
python main.py --port COM3 --gui
```

GUIでは以下の機能が利用できます：

1. **接続設定**
   - ポート選択
   - ボーレート設定
   - 接続/切断

2. **ファイルブラウザ**
   - ファイル/ディレクトリ一覧表示
   - アップロード/ダウンロード
   - ファイル/ディレクトリ作成・削除
   - ファイル情報表示

3. **デバイス情報**
   - ヒープ使用状況
   - SDカード情報
   - 起動時間
   - デバイスリセット

4. **ログ表示**
   - 操作ログ
   - エラーメッセージ
   - デバッグ情報

## 単体テスト

各モジュールは単体でテスト実行できます：

### UARTクライアントのテスト

```bash
python uart_client.py --port COM3 --debug
```

### CLIのテスト

```bash
python cli.py COM3
```

### GUIのテスト

```bash
python gui.py COM3
```

## トラブルシューティング

1. **接続エラー**
   - ポート名が正しいか確認してください
   - 他のアプリケーションがポートを使用していないか確認してください
   - デバイスのUSB接続を確認してください

2. **通信エラー**
   - ボーレートがデバイス側と一致しているか確認してください
   - `--debug` オプションを使用してプロトコル通信を詳細に確認してください
   - デバイスが正しいファームウェアを実行しているか確認してください

3. **CRC不一致エラー**
   - ノイズがある可能性があります。接続ケーブルを確認してください
   - デバイス側のプロトコル実装に問題がある可能性があります

4. **GUIが起動しない**
   - tkinterが正しくインストールされているか確認してください

## プロトコル仕様

本ツールはM5Paper S3 UARTファイル転送プロトコルに準拠しています。プロトコルは以下の通りです：

### パケット構造

```
[START_MARKER][COMMAND/RESPONSE][DATA_LENGTH_L][DATA_LENGTH_H][DATA...][CRC_L][CRC_H][END_MARKER]
```

- START_MARKER: 0xAA
- END_MARKER: 0x55
- CRC: MODBUS CRC-16（多項式：0xA001）

詳細なプロトコル仕様については「M5Paper S3 UARTファイル転送プロトコル仕様書」を参照してください。
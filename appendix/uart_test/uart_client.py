import serial
import struct
import time
import logging

# ロガーの設定
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('UARTClient')

# パケットマーカー
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

# レスポンスコードの名称マッピング
RESPONSE_NAMES = {
    RESP_OK: "OK",
    RESP_ERROR: "ERROR",
    RESP_FILE_NOT_FOUND: "FILE_NOT_FOUND",
    RESP_DISK_FULL: "DISK_FULL",
    RESP_INVALID_PARAM: "INVALID_PARAM"
}

class UARTClient:
    def __init__(self, port=None, baudrate=115200, timeout=5):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.serial = None
        self.is_connected = False
        self.debug_mode = False
    
    def set_debug(self, debug=True):
        """デバッグモードを設定"""
        self.debug_mode = debug
        if debug:
            logger.setLevel(logging.DEBUG)
        else:
            logger.setLevel(logging.INFO)
    
    def connect(self, port=None, baudrate=None):
        """シリアルポートに接続"""
        if port:
            self.port = port
        if baudrate:
            self.baudrate = baudrate
            
        if not self.port:
            raise ValueError("ポートが指定されていません")
        
        try:
            self.serial = serial.Serial(
                self.port,
                self.baudrate,
                timeout=self.timeout,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE
            )
            self.is_connected = True
            logger.info(f"ポート {self.port} に接続しました（ボーレート: {self.baudrate}）")
            return True
        except serial.SerialException as e:
            logger.error(f"接続エラー: {str(e)}")
            self.is_connected = False
            return False
    
    def disconnect(self):
        """シリアルポートの接続を切断"""
        if self.serial and self.serial.is_open:
            self.serial.close()
            self.is_connected = False
            logger.info("接続を切断しました")
    
    def calculate_crc16(self, data):
        """CRC-16を計算する (MODBUSアルゴリズム)"""
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
        """コマンドを送信し、レスポンスを受信する"""
        if not self.is_connected:
            logger.error("コマンド送信エラー: 接続されていません")
            return None, None
        
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
        
        if self.debug_mode:
            logger.debug(f"送信パケット: {packet.hex()}")
            logger.debug(f"コマンド: 0x{command:02X}, データ長: {data_length}")
        
        # 送信
        self.serial.reset_input_buffer()  # 入力バッファをクリア
        start_time = time.time()
        self.serial.write(packet)
        
        # レスポンス受信
        response_code, response_data = self._receive_response()
        
        if self.debug_mode:
            elapsed = time.time() - start_time
            logger.debug(f"応答時間: {elapsed:.3f}秒")
            if response_code is not None:
                resp_name = RESPONSE_NAMES.get(response_code, f"UNKNOWN(0x{response_code:02X})")
                logger.debug(f"レスポンス: {resp_name}, データ長: {len(response_data) if response_data else 0}")
        
        return response_code, response_data
    
    def _receive_response(self):
        """レスポンスパケットを受信して解析する"""
        state = 0  # 0=マーカー待ち, 1=コード, 2=長さL, 3=長さH, 4=データ, 5=CRCL, 6=CRCH, 7=終了マーカー
        response_code = 0
        data_length = 0
        response_data = bytearray()
        crc = 0
        
        timeout = time.time() + self.timeout
        
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
                            logger.error(f"CRC不一致: 計算値={calc_crc:04X}, 受信値={crc:04X}")
                            return None, None
                    else:
                        logger.error(f"不正な終了マーカー: 0x{b:02X}")
                        return None, None
            
            time.sleep(0.01)
        
        logger.error("タイムアウト: レスポンスの受信に失敗しました")
        return None, None
    
    #
    # 基本コマンド
    #
    
    def ping(self):
        """デバイス状態確認"""
        logger.info("デバイス状態確認を実行中...")
        resp_code, data = self.send_command(CMD_PING)
        
        if resp_code == RESP_OK and len(data) >= 17:
            # データ解析
            heap_free = struct.unpack('<I', data[0:4])[0]
            sd_mounted = data[4] > 0
            sd_total = struct.unpack('<Q', data[5:13])[0]
            sd_free = struct.unpack('<Q', data[13:21])[0]
            uptime = struct.unpack('<I', data[21:25])[0] if len(data) >= 25 else 0
            
            result = {
                'heap_free': heap_free,
                'sd_mounted': sd_mounted,
                'sd_total': sd_total,
                'sd_free': sd_free,
                'uptime': uptime
            }
            
            logger.info(f"デバイス状態: ヒープ空き={heap_free}バイト, "
                      f"SDカード={'マウント済み' if sd_mounted else '未マウント'}, "
                      f"SD総容量={sd_total/1024/1024:.1f}MB, "
                      f"SD空き容量={sd_free/1024/1024:.1f}MB, "
                      f"起動時間={uptime}秒")
            return result
        else:
            logger.error(f"デバイス状態確認失敗: {RESPONSE_NAMES.get(resp_code, 'UNKNOWN')}")
            return None
    
    def reset(self):
        """デバイスリセット"""
        logger.info("デバイスリセットを実行中...")
        try:
            resp_code, _ = self.send_command(CMD_RESET)
            success = resp_code == RESP_OK
        except:
            # リセット後に応答が来ない場合もあるので成功とみなす
            logger.info("デバイスリセットコマンド送信後に接続が切れました（正常な動作です）")
            success = True
        
        if success:
            logger.info("デバイスリセット成功")
        else:
            logger.error("デバイスリセット失敗")
        
        return success
    
    #
    # ファイル操作コマンド
    #
    
    def get_file_list(self, path="/"):
        """ファイル/フォルダ一覧取得"""
        logger.info(f"ファイル一覧取得中: {path}")
        resp_code, data = self.send_command(CMD_FILE_LIST, path.encode('utf-8'))
        
        if resp_code == RESP_OK:
            files = []
            pos = 0
            
            while pos < len(data):
                if pos + 10 > len(data):
                    break
                
                file_type = 'directory' if data[pos] else 'file'
                file_size = struct.unpack('<I', data[pos+1:pos+5])[0]
                modified_time = struct.unpack('<I', data[pos+5:pos+9])[0]
                name_len = data[pos+9]
                
                if pos + 10 + name_len > len(data):
                    break
                
                name = data[pos+10:pos+10+name_len].decode('utf-8')
                
                files.append({
                    'name': name,
                    'type': file_type,
                    'size': file_size,
                    'modified': modified_time
                })
                
                pos += 10 + name_len
            
            logger.info(f"{len(files)}個のファイル/フォルダを取得しました")
            return files
        else:
            logger.error(f"ファイル一覧取得失敗: {RESPONSE_NAMES.get(resp_code, 'UNKNOWN')}")
            return None
    
    def get_file_info(self, path):
        """ファイル情報取得"""
        logger.info(f"ファイル情報取得中: {path}")
        resp_code, data = self.send_command(CMD_FILE_INFO, path.encode('utf-8'))
        
        if resp_code == RESP_OK and len(data) >= 13:
            file_type = 'directory' if data[0] else 'file'
            size = struct.unpack('<I', data[1:5])[0]
            created = struct.unpack('<I', data[5:9])[0]
            modified = struct.unpack('<I', data[9:13])[0]
            
            info = {
                'type': file_type,
                'size': size,
                'created': created,
                'modified': modified
            }
            
            logger.info(f"ファイル情報: タイプ={file_type}, サイズ={size}バイト, "
                      f"作成日時={time.ctime(created)}, 更新日時={time.ctime(modified)}")
            return info
        else:
            logger.error(f"ファイル情報取得失敗: {RESPONSE_NAMES.get(resp_code, 'UNKNOWN')}")
            return None
    
    def file_exists(self, path):
        """ファイル存在確認"""
        logger.info(f"ファイル存在確認中: {path}")
        resp_code, data = self.send_command(CMD_FILE_EXIST, path.encode('utf-8'))
        
        if resp_code == RESP_OK and len(data) >= 2:
            exists = data[0] > 0
            file_type = 'directory' if data[1] else 'file'
            
            result = {'exists': exists, 'type': file_type if exists else None}
            
            if exists:
                logger.info(f"パス '{path}' は存在します (タイプ: {file_type})")
            else:
                logger.info(f"パス '{path}' は存在しません")
            
            return result
        else:
            logger.error(f"ファイル存在確認失敗: {RESPONSE_NAMES.get(resp_code, 'UNKNOWN')}")
            return {'exists': False, 'type': None}
    
    def upload_file(self, local_path, remote_path, callback=None):
        """ファイルアップロード"""
        # モード1=書込
        logger.info(f"ファイルアップロード: {local_path} → {remote_path}")
        
        try:
            # ファイルを読み込み
            with open(local_path, 'rb') as f:
                file_size = f.seek(0, 2)  # ファイルサイズ取得
                f.seek(0)                # 先頭に戻る
                
                # ファイルオープン
                cmd_data = bytes([1]) + remote_path.encode('utf-8')  # モード1=書込
                resp_code, _ = self.send_command(CMD_FILE_OPEN, cmd_data)
                
                if resp_code != RESP_OK:
                    logger.error(f"ファイルオープン失敗: {RESPONSE_NAMES.get(resp_code, 'UNKNOWN')}")
                    return False
                
                # チャンク単位で転送
                chunk_size = 1024  # 1KBずつ転送
                total_sent = 0
                
                while True:
                    chunk = f.read(chunk_size)
                    if not chunk:
                        break
                    
                    # データ転送
                    cmd_data = bytes([1]) + chunk  # モード1=書込
                    resp_code, _ = self.send_command(CMD_FILE_DATA, cmd_data)
                    
                    if resp_code != RESP_OK:
                        logger.error(f"ファイル書き込み失敗: {RESPONSE_NAMES.get(resp_code, 'UNKNOWN')}")
                        self.send_command(CMD_FILE_CLOSE)  # 失敗してもファイルを閉じる
                        return False
                    
                    total_sent += len(chunk)
                    
                    # 進捗コールバック
                    if callback:
                        callback(total_sent, file_size)
                    
                    # 進捗ログ
                    if self.debug_mode or total_sent % (chunk_size * 10) == 0:
                        logger.info(f"アップロード進捗: {total_sent}/{file_size} バイト ({total_sent/file_size*100:.1f}%)")
                
                # ファイルクローズ
                resp_code, _ = self.send_command(CMD_FILE_CLOSE)
                if resp_code != RESP_OK:
                    logger.error(f"ファイルクローズ失敗: {RESPONSE_NAMES.get(resp_code, 'UNKNOWN')}")
                    return False
                
                logger.info(f"アップロード完了: {total_sent}バイト")
                return True
                
        except Exception as e:
            logger.error(f"アップロードエラー: {str(e)}")
            # エラー時はファイルを閉じる処理を試みる
            try:
                self.send_command(CMD_FILE_CLOSE)
            except:
                pass
            return False
    
    def download_file(self, remote_path, local_path, callback=None):
        """ファイルダウンロード"""
        # モード0=読込
        logger.info(f"ファイルダウンロード: {remote_path} → {local_path}")
        
        try:
            # ファイルオープン
            cmd_data = bytes([0]) + remote_path.encode('utf-8')  # モード0=読込
            resp_code, _ = self.send_command(CMD_FILE_OPEN, cmd_data)
            
            if resp_code != RESP_OK:
                logger.error(f"ファイルオープン失敗: {RESPONSE_NAMES.get(resp_code, 'UNKNOWN')}")
                return False
            
            # 出力ファイルを開く
            with open(local_path, 'wb') as f:
                total_size = 0
                chunk_size = 1024  # 1KBずつ要求
                eof = False
                
                while not eof:
                    # データ要求
                    cmd_data = struct.pack('<BH', 0, chunk_size)  # モード0=読込, サイズ
                    resp_code, data = self.send_command(CMD_FILE_DATA, cmd_data)
                    
                    if resp_code != RESP_OK:
                        logger.error(f"ファイル読み込み失敗: {RESPONSE_NAMES.get(resp_code, 'UNKNOWN')}")
                        self.send_command(CMD_FILE_CLOSE)  # 失敗してもファイルを閉じる
                        return False
                    
                    # EOFフラグとデータを分離
                    eof = data[0] > 0
                    chunk = data[1:]
                    
                    # ファイルに書き込み
                    f.write(chunk)
                    
                    total_size += len(chunk)
                    
                    # 進捗コールバック
                    if callback:
                        callback(total_size, None)  # 合計サイズは不明なのでNone
                    
                    # 進捗ログ (1MB毎かつデバッグモード時)
                    if self.debug_mode or total_size % (1024 * 1024) < chunk_size:
                        logger.info(f"ダウンロード進捗: {total_size}バイト")
            
            # ファイルクローズ
            resp_code, _ = self.send_command(CMD_FILE_CLOSE)
            if resp_code != RESP_OK:
                logger.error(f"ファイルクローズ失敗: {RESPONSE_NAMES.get(resp_code, 'UNKNOWN')}")
                return False
            
            logger.info(f"ダウンロード完了: {total_size}バイト")
            return True
            
        except Exception as e:
            logger.error(f"ダウンロードエラー: {str(e)}")
            # エラー時はファイルを閉じる処理を試みる
            try:
                self.send_command(CMD_FILE_CLOSE)
            except:
                pass
            return False
    
    def delete_file(self, path):
        """ファイル削除"""
        logger.info(f"ファイル削除中: {path}")
        resp_code, _ = self.send_command(CMD_FILE_DELETE, path.encode('utf-8'))
        
        if resp_code == RESP_OK:
            logger.info(f"ファイル '{path}' を削除しました")
            return True
        else:
            logger.error(f"ファイル削除失敗: {RESPONSE_NAMES.get(resp_code, 'UNKNOWN')}")
            return False
    
    def create_directory(self, path):
        """ディレクトリ作成"""
        logger.info(f"ディレクトリ作成中: {path}")
        resp_code, _ = self.send_command(CMD_DIR_CREATE, path.encode('utf-8'))
        
        if resp_code == RESP_OK:
            logger.info(f"ディレクトリ '{path}' を作成しました")
            return True
        else:
            logger.error(f"ディレクトリ作成失敗: {RESPONSE_NAMES.get(resp_code, 'UNKNOWN')}")
            return False
    
    def delete_directory(self, path):
        """ディレクトリ削除（再帰的）"""
        logger.info(f"ディレクトリ削除中: {path}")
        resp_code, _ = self.send_command(CMD_DIR_DELETE, path.encode('utf-8'))
        
        if resp_code == RESP_OK:
            logger.info(f"ディレクトリ '{path}' を削除しました")
            return True
        else:
            logger.error(f"ディレクトリ削除失敗: {RESPONSE_NAMES.get(resp_code, 'UNKNOWN')}")
            return False

# 単体テスト用
if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description='UARTクライアントテスト')
    parser.add_argument('--port', type=str, help='シリアルポート (例: COM3, /dev/ttyUSB0)')
    parser.add_argument('--baudrate', type=int, default=115200, help='ボーレート (デフォルト: 115200)')
    parser.add_argument('--debug', action='store_true', help='デバッグモード有効')
    
    args = parser.parse_args()
    
    if not args.port:
        print("エラー: シリアルポートを指定してください")
        parser.print_help()
        exit(1)
    
    # クライアント作成
    client = UARTClient(args.port, args.baudrate)
    client.set_debug(args.debug)
    
    if client.connect():
        try:
            # PINGテスト
            print("\n===== PINGテスト =====")
            result = client.ping()
            print(f"PING結果: {result}")
            
            # ファイル一覧テスト
            print("\n===== ファイル一覧テスト =====")
            files = client.get_file_list("/")
            if files:
                print("ファイル一覧:")
                for file in files:
                    print(f"  {file['name']} - {file['type']} ({file['size']} バイト)")
            
            # ここでさらにテストを追加できます
            
        finally:
            client.disconnect()
            print("テスト完了")
    else:
        print(f"ポート {args.port} への接続に失敗しました")
"""
ESP32 UART File Transfer Test Tool - CLI Interface

コマンドラインインターフェース実装
"""

import cmd
import os
import time
import sys
from uart_client import UARTClient
import logging

logger = logging.getLogger('UARTToolCLI')

class UARTToolCLI(cmd.Cmd):
    """ESP32 UART File Transfer Test Tool の対話型コマンドライン"""
    
    intro = "ESP32 UART File Transfer Test Tool\n入力例: help, ping, ls /\nquit で終了"
    prompt = "uart-tool> "
    
    def __init__(self, port=None, baudrate=115200, debug=False):
        super().__init__()
        self.client = UARTClient(port, baudrate)
        self.client.set_debug(debug)
        
        if port:
            self.do_connect(port)
    
    def run(self):
        try:
            self.cmdloop()
        except KeyboardInterrupt:
            print("\n終了します...")
        finally:
            self.do_quit("")
    
    def emptyline(self):
        """空行は何もしない"""
        pass
    
    def default(self, line):
        """未知のコマンド"""
        print(f"コマンドが認識できません: {line}")
        print("利用可能なコマンドを確認するには help を入力してください")
    
    def do_connect(self, arg):
        """シリアルポートに接続する
        使い方: connect [PORT] [BAUDRATE]
        例: connect COM3
            connect /dev/ttyUSB0 115200
        """
        parts = arg.split()
        port = parts[0] if parts else None
        baudrate = int(parts[1]) if len(parts) > 1 else 115200
        
        if not port:
            print("ポートを指定してください")
            return
        
        print(f"接続中: {port} ({baudrate} bps)...")
        if self.client.connect(port, baudrate):
            self.prompt = f"uart-tool({port})> "
            print("接続しました")
        else:
            print("接続に失敗しました")
    
    def do_disconnect(self, arg):
        """シリアルポートから切断する
        使い方: disconnect
        """
        self.client.disconnect()
        self.prompt = "uart-tool> "
        print("切断しました")
    
    def do_ping(self, arg):
        """デバイス状態を確認する
        使い方: ping
        """
        if not self._check_connection():
            return
        
        result = self.client.ping()
        if result:
            print("\nデバイス状態:")
            print(f"  ヒープメモリ空き: {result['heap_free']:,} バイト")
            print(f"  SDカード: {'マウント済み' if result['sd_mounted'] else '未マウント'}")
            if result['sd_mounted']:
                print(f"  SD総容量: {result['sd_total']:,} バイト ({result['sd_total']/1024/1024:.1f} MB)")
                print(f"  SD空き容量: {result['sd_free']:,} バイト ({result['sd_free']/1024/1024:.1f} MB)")
            print(f"  起動時間: {result['uptime']} 秒 ({result['uptime']/60/60:.1f} 時間)")
    
    def do_reset(self, arg):
        """デバイスをリセットする
        使い方: reset
        """
        if not self._check_connection():
            return
        
        print("デバイスをリセットしています...")
        if self.client.reset():
            print("リセット成功")
            self.client.disconnect()
            self.prompt = "uart-tool> "
            print("接続が切断されました")
        else:
            print("リセット失敗")
    
    def do_ls(self, arg):
        """ディレクトリの内容を表示する
        使い方: ls [PATH]
        例: ls /
            ls /data
        """
        if not self._check_connection():
            return
        
        path = arg if arg else "/"
        print(f"ディレクトリ内容を取得中: {path}")
        
        files = self.client.get_file_list(path)
        if files is not None:
            # ファイルとディレクトリを分離
            dirs = [f for f in files if f['type'] == 'directory']
            regular_files = [f for f in files if f['type'] == 'file']
            
            # ディレクトリ一覧
            if dirs:
                print("\nディレクトリ:")
                for d in sorted(dirs, key=lambda x: x['name']):
                    mod_time = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(d['modified']))
                    print(f"  [DIR] {d['name']:30} {mod_time}")
            
            # ファイル一覧
            if regular_files:
                print("\nファイル:")
                for f in sorted(regular_files, key=lambda x: x['name']):
                    size_str = f"{f['size']:,} B"
                    kb_size = f['size'] / 1024
                    if kb_size >= 1:
                        size_str += f" ({kb_size:.1f} KB)"
                    if kb_size >= 1024:
                        mb_size = kb_size / 1024
                        size_str += f" ({mb_size:.1f} MB)"
                    
                    mod_time = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(f['modified']))
                    print(f"  {f['name']:30} {size_str:20} {mod_time}")
            
            # 合計
            print(f"\n合計: {len(dirs)}個のディレクトリ, {len(regular_files)}個のファイル")
        else:
            print("ディレクトリの内容を取得できませんでした")
    
    def do_info(self, arg):
        """ファイル情報を表示する
        使い方: info PATH
        例: info /data.txt
        """
        if not self._check_connection() or not arg:
            print("ファイルパスを指定してください")
            return
        
        info = self.client.get_file_info(arg)
        if info:
            print(f"\nファイル情報: {arg}")
            print(f"  タイプ: {info['type']}")
            if info['type'] == 'file':
                print(f"  サイズ: {info['size']:,} バイト ({info['size']/1024:.1f} KB)")
            print(f"  作成日時: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(info['created']))}")
            print(f"  更新日時: {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(info['modified']))}")
    
    def do_exists(self, arg):
        """ファイルまたはディレクトリの存在を確認する
        使い方: exists PATH
        例: exists /data.txt
        """
        if not self._check_connection() or not arg:
            print("パスを指定してください")
            return
        
        result = self.client.file_exists(arg)
        if result['exists']:
            print(f"パス '{arg}' は存在します (タイプ: {result['type']})")
        else:
            print(f"パス '{arg}' は存在しません")
    
    def do_upload(self, arg):
        """ローカルファイルをデバイスにアップロードする
        使い方: upload LOCAL_PATH REMOTE_PATH
        例: upload data.txt /data.txt
        """
        if not self._check_connection():
            return
        
        parts = arg.split()
        if len(parts) < 2:
            print("使い方: upload LOCAL_PATH REMOTE_PATH")
            return
        
        local_path = parts[0]
        remote_path = parts[1]
        
        if not os.path.exists(local_path):
            print(f"ローカルファイル '{local_path}' が見つかりません")
            return
        
        if os.path.isdir(local_path):
            print(f"'{local_path}' はディレクトリです。ファイルを指定してください")
            return
        
        file_size = os.path.getsize(local_path)
        print(f"アップロード: {local_path} → {remote_path} ({file_size:,} バイト)")
        
        # 進捗表示関数
        def show_progress(current, total):
            percent = (current / total) * 100
            bar_length = 50
            filled_length = int(bar_length * current // total)
            bar = '=' * filled_length + '-' * (bar_length - filled_length)
            
            sys.stdout.write(f'\r[{bar}] {percent:.1f}% ({current:,}/{total:,} バイト)')
            sys.stdout.flush()
            
            if current == total:
                print()
        
        success = self.client.upload_file(local_path, remote_path, show_progress)
        
        if success:
            print(f"アップロード成功: {remote_path}")
        else:
            print(f"アップロード失敗: {remote_path}")
    
    def do_download(self, arg):
        """デバイスからローカルにファイルをダウンロードする
        使い方: download REMOTE_PATH LOCAL_PATH
        例: download /data.txt downloaded.txt
        """
        if not self._check_connection():
            return
        
        parts = arg.split()
        if len(parts) < 2:
            print("使い方: download REMOTE_PATH LOCAL_PATH")
            return
        
        remote_path = parts[0]
        local_path = parts[1]
        
        # ファイルの存在確認
        result = self.client.file_exists(remote_path)
        if not result['exists']:
            print(f"リモートファイル '{remote_path}' が見つかりません")
            return
        
        if result['type'] != 'file':
            print(f"'{remote_path}' はファイルではありません")
            return
        
        # ローカルファイルの存在確認
        if os.path.exists(local_path):
            confirm = input(f"'{local_path}' は既に存在します。上書きしますか？ (y/n): ")
            if confirm.lower() != 'y':
                print("ダウンロードをキャンセルしました")
                return
        
        print(f"ダウンロード: {remote_path} → {local_path}")
        
        # 進捗表示関数
        downloaded_size = 0
        start_time = time.time()
        
        def show_progress(current, total):
            nonlocal downloaded_size, start_time
            downloaded_size = current
            
            elapsed = time.time() - start_time
            if elapsed > 0:
                speed = current / elapsed
                speed_str = f"{speed:.1f} バイト/秒"
                if speed >= 1024:
                    speed_str = f"{speed/1024:.1f} KB/秒"
                if speed >= 1024*1024:
                    speed_str = f"{speed/1024/1024:.1f} MB/秒"
            else:
                speed_str = "計算中..."
            
            sys.stdout.write(f'\rダウンロード中: {current:,} バイト ({speed_str})')
            sys.stdout.flush()
        
        success = self.client.download_file(remote_path, local_path, show_progress)
        
        print()  # 改行
        if success:
            elapsed = time.time() - start_time
            if elapsed > 0:
                speed = downloaded_size / elapsed
                speed_str = f"{speed:.1f} バイト/秒"
                if speed >= 1024:
                    speed_str = f"{speed/1024:.1f} KB/秒"
                if speed >= 1024*1024:
                    speed_str = f"{speed/1024/1024:.1f} MB/秒"
            else:
                speed_str = "計算不能"
            
            print(f"ダウンロード成功: {local_path} ({downloaded_size:,} バイト, {speed_str})")
        else:
            print(f"ダウンロード失敗: {local_path}")
    
    def do_rm(self, arg):
        """ファイルを削除する
        使い方: rm PATH
        例: rm /data.txt
        """
        if not self._check_connection() or not arg:
            print("削除するファイルのパスを指定してください")
            return
        
        # 存在確認
        result = self.client.file_exists(arg)
        if not result['exists']:
            print(f"ファイル '{arg}' が見つかりません")
            return
        
        if result['type'] == 'directory':
            print(f"'{arg}' はディレクトリです。ファイルを指定してください")
            return
        
        # 確認
        confirm = input(f"ファイル '{arg}' を削除しますか？ (y/n): ")
        if confirm.lower() != 'y':
            print("削除をキャンセルしました")
            return
        
        if self.client.delete_file(arg):
            print(f"ファイル '{arg}' を削除しました")
        else:
            print(f"ファイル '{arg}' の削除に失敗しました")
    
    def do_mkdir(self, arg):
        """ディレクトリを作成する
        使い方: mkdir PATH
        例: mkdir /new_folder
        """
        if not self._check_connection() or not arg:
            print("作成するディレクトリのパスを指定してください")
            return
        
        if self.client.create_directory(arg):
            print(f"ディレクトリ '{arg}' を作成しました")
        else:
            print(f"ディレクトリ '{arg}' の作成に失敗しました")
    
    def do_rmdir(self, arg):
        """ディレクトリを削除する（再帰的）
        使い方: rmdir PATH
        例: rmdir /old_folder
        """
        if not self._check_connection() or not arg:
            print("削除するディレクトリのパスを指定してください")
            return
        
        # 存在確認
        result = self.client.file_exists(arg)
        if not result['exists']:
            print(f"ディレクトリ '{arg}' が見つかりません")
            return
        
        if result['type'] != 'directory':
            print(f"'{arg}' はディレクトリではありません")
            return
        
        # 確認
        confirm = input(f"ディレクトリ '{arg}' を再帰的に削除しますか？ (y/n): ")
        if confirm.lower() != 'y':
            print("削除をキャンセルしました")
            return
        
        if self.client.delete_directory(arg):
            print(f"ディレクトリ '{arg}' を削除しました")
        else:
            print(f"ディレクトリ '{arg}' の削除に失敗しました")
    
    def do_exit(self, arg):
        """プログラムを終了する
        使い方: exit
        """
        return self.do_quit(arg)
    
    def do_quit(self, arg):
        """プログラムを終了する
        使い方: quit
        """
        print("終了します...")
        self.client.disconnect()
        return True
    
    # エイリアス
    do_q = do_quit
    
    def _check_connection(self):
        """接続チェック"""
        if not self.client.is_connected:
            print("デバイスに接続されていません")
            print("接続するには connect PORT [BAUDRATE] を使用してください")
            return False
        return True

# 単体テスト用
if __name__ == "__main__":
    import sys
    
    if len(sys.argv) > 1:
        cli = UARTToolCLI(sys.argv[1])
    else:
        cli = UARTToolCLI()
    
    cli.run()
#!/usr/bin/env python3
"""
ESP32 UART File Transfer Test Tool

ESP32マイコンとPC間のUART通信によるファイル転送をテストするためのツール
"""

import argparse
import sys
import logging
from uart_client import UARTClient
# ロガーの初期化を関数の外に移動
logger = logging.getLogger('UARTTool')

def main():
    """メインエントリーポイント"""
    # コマンドライン引数の設定
    parser = argparse.ArgumentParser(description='ESP32 UART File Transfer Test Tool')
    parser.add_argument('--port', type=str, help='シリアルポート (例: COM3, /dev/ttyUSB0)')
    parser.add_argument('--baudrate', type=int, default=115200, help='ボーレート (デフォルト: 115200)')
    parser.add_argument('--debug', action='store_true', help='デバッグモード有効')
    parser.add_argument('--gui', action='store_true', help='GUIモード有効')
    
    args = parser.parse_args()
    
    # ロガーの設定
    log_level = logging.DEBUG if args.debug else logging.INFO
    logging.basicConfig(
        level=log_level,
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
    )
    logger = logging.getLogger('UARTTool')
    
    logger.info("ESP32 UART File Transfer Test Tool 起動中...")
    
    if args.gui:
        try:
            from gui import UARTToolGUI
            app = UARTToolGUI(args.port, args.baudrate, args.debug)
            app.run()
        except ImportError:
            logger.error("GUIモジュールをロードできませんでした")
            logger.info("CLIモードで起動します")
            run_cli(args)
        except Exception as e:
            logger.error(f"GUIモードでのエラー: {str(e)}")
            logger.info("CLIモードで起動します")
            run_cli(args)
    else:
        run_cli(args)

def run_cli(args):
    """CLIモードでの実行"""
    try:
        from cli import UARTToolCLI
        cli = UARTToolCLI(args.port, args.baudrate, args.debug)
        cli.run()
    except ImportError:
        logger.error("CLIモジュールをロードできませんでした")
        logger.error("uart_client.pyの基本テストモードで実行します")
        
        # 基本テストモード
        if args.port:
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
                    
                finally:
                    client.disconnect()
        else:
            print("エラー: シリアルポートを指定してください (--port)")
            sys.exit(1)

if __name__ == "__main__":
    main()
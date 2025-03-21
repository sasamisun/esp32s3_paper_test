#!/usr/bin/env python3
"""
PNG to EPD 4-bit Grayscale Converter

このスクリプトはPNG画像を電子ペーパーディスプレイ用の4ビットグレースケールに変換します。
明るさ調整、コントラスト調整、反転などのオプションが利用可能です。
画像の自動トリミング機能も追加されています。
"""

from PIL import Image, ImageEnhance
import sys
import os
import argparse

def convert_png_to_4bit_grayscale(input_file, output_file, brightness=1.0, contrast=1.0, 
                                  invert=False, black_threshold=None, white_threshold=None, 
                                  gamma=1.0, transparent_white=True, trim_width=540, trim_height=960):
    if trim_width is None:
        trim_width=540
    if trim_height is None:
        trim_height=960
    """
    PNG画像を電子ペーパーディスプレイ用の4ビットグレースケールに変換します
    
    Parameters:
    - input_file: 入力PNG画像ファイルパス
    - output_file: 出力Cヘッダファイルパス
    - brightness: 明るさ調整 (0.0-2.0, 1.0が元の明るさ)
    - contrast: コントラスト調整 (0.0-2.0, 1.0が元のコントラスト)
    - invert: 白黒反転するかどうか
    - black_threshold: この値以下は黒として扱う (0-255)
    - white_threshold: この値以上は白として扱う (0-255)
    - gamma: ガンマ補正値 (1.0がデフォルト)
    - transparent_white: 透明部分を白として扱うかどうか
    - trim_width: トリミング後の幅 (None=トリミングなし)
    - trim_height: トリミング後の高さ (None=トリミングなし)
    """
    # 画像を開く
    try:
        img = Image.open(input_file)
    except Exception as e:
        print(f"エラー: 画像を開けませんでした - {e}")
        return False
    
    # 元の画像サイズを取得
    original_width, original_height = img.size
    print(f"Original image dimensions: {original_width} x {original_height}")
    
    # トリミング処理
    # トリミングサイズが元のサイズより大きい場合は元のサイズに制限
    trim_width = min(trim_width, original_width)
    trim_height = min(trim_height, original_height)
    
    # 元のサイズと違う場合だけトリミング処理を実行
    if trim_width != original_width or trim_height != original_height:
        # 左上を基準に右端と下側をトリミング
        img = img.crop((0, 0, trim_width, trim_height))
        print(f"Image trimmed to: {trim_width} x {trim_height}")
    else:
        print(f"No trimming needed, using original dimensions: {original_width} x {original_height}")
    
    # RGBAモードに変換（透明度情報を確保）
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
    
    # 明るさ調整
    if brightness != 1.0:
        enhancer = ImageEnhance.Brightness(img)
        img = enhancer.enhance(brightness)
    
    # コントラスト調整
    if contrast != 1.0:
        enhancer = ImageEnhance.Contrast(img)
        img = enhancer.enhance(contrast)
    
    width, height = img.size
    print(f"Final image dimensions: {width} x {height}")
    
    # 4ビットグレースケールに変換（16階調）
    bytes_data = []
    for y in range(height):
        for x in range(0, width, 2):  # 2ピクセルずつ処理
            # 1つ目のピクセル
            r, g, b, a = img.getpixel((x, y))
            
            # 透明ピクセルの処理
            if a == 0:
                if transparent_white:
                    gray1 = 15  # 白
                else:
                    gray1 = 0   # 黒
            else:
                # RGB→グレースケール変換（一般的な重み付け）
                gray = int(0.299 * r + 0.587 * g + 0.114 * b)
                
                # ガンマ補正
                if gamma != 1.0:
                    gray = int(255 * ((gray / 255) ** (1/gamma)))
                    gray = max(0, min(255, gray))
                
                # しきい値処理
                if black_threshold is not None and gray <= black_threshold:
                    gray = 0
                elif white_threshold is not None and gray >= white_threshold:
                    gray = 255
                
                # 0-255を0-15に変換
                gray1 = min(15, gray >> 4)
                
                # 反転
                if invert:
                    gray1 = 15 - gray1
            
            # 2つ目のピクセル（画像の幅が奇数の場合は白または指定された背景色を使用）
            if x + 1 < width:
                r, g, b, a = img.getpixel((x + 1, y))
                if a == 0:
                    if transparent_white:
                        gray2 = 15  # 白
                    else:
                        gray2 = 0   # 黒
                else:
                    gray = int(0.299 * r + 0.587 * g + 0.114 * b)
                    
                    # ガンマ補正
                    if gamma != 1.0:
                        gray = int(255 * ((gray / 255) ** (1/gamma)))
                        gray = max(0, min(255, gray))
                    
                    # しきい値処理
                    if black_threshold is not None and gray <= black_threshold:
                        gray = 0
                    elif white_threshold is not None and gray >= white_threshold:
                        gray = 255
                    
                    gray2 = min(15, gray >> 4)
                    
                    # 反転
                    if invert:
                        gray2 = 15 - gray2
            else:
                # 画像の端の場合
                gray2 = 15 if transparent_white else 0
            
            # 2つのピクセルを1バイトに格納（下位4ビットが1つ目、上位4ビットが2つ目）
            byte = gray1 | (gray2 << 4)
            bytes_data.append(byte)
    
    # Cの配列としてファイルに出力
    try:
        with open(output_file, 'w') as f:
            # ヘッダコメント
            f.write(f"// 4-bit grayscale image data converted from {os.path.basename(input_file)}\n")
            f.write(f"// Original dimensions: {original_width} x {original_height}\n")
            f.write(f"// Trimmed dimensions: {width} x {height}\n")
            f.write(f"// Conversion options:\n")
            f.write(f"//  - Brightness: {brightness}\n")
            f.write(f"//  - Contrast: {contrast}\n")
            f.write(f"//  - Inverted: {invert}\n")
            if black_threshold is not None:
                f.write(f"//  - Black threshold: {black_threshold}\n")
            if white_threshold is not None:
                f.write(f"//  - White threshold: {white_threshold}\n")
            f.write(f"//  - Gamma: {gamma}\n")
            f.write(f"//  - Transparent as white: {transparent_white}\n\n")
            
            # ヘッダインクルード
            f.write(f"#include <stdint.h>\n\n")
            
            # サイズ定義
            f.write(f"#define LOGO_WIDTH {width}\n")
            f.write(f"#define LOGO_HEIGHT {height}\n\n")
            
            # データ配列
            f.write("const uint8_t logo_data[] = {\n    ")
            
            # バイト値を16進数で出力
            for i, byte in enumerate(bytes_data):
                f.write(f"0x{byte:02X}")
                if i < len(bytes_data) - 1:
                    f.write(", ")
                if (i + 1) % 12 == 0:  # 12バイトごとに改行
                    f.write("\n    ")
            
            f.write("\n};\n")
            f.write(f"const uint32_t logo_data_len = {len(bytes_data)};\n")
        
        print(f"変換成功: {output_file} に保存しました")
        return True
    
    except Exception as e:
        print(f"エラー: ファイル書き込み中 - {e}")
        return False

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='PNG画像を電子ペーパーディスプレイ用の4ビットグレースケールに変換します')
    parser.add_argument('input', help='入力PNG画像ファイル')
    parser.add_argument('output', help='出力Cヘッダファイル')
    parser.add_argument('-b', '--brightness', type=float, default=1.0, help='明るさ調整 (0.0-2.0, 1.0が元の明るさ)')
    parser.add_argument('-c', '--contrast', type=float, default=1.0, help='コントラスト調整 (0.0-2.0, 1.0が元のコントラスト)')
    parser.add_argument('-i', '--invert', action='store_true', help='白黒反転する')
    parser.add_argument('--black-threshold', type=int, help='この値以下は黒として扱う (0-255)')
    parser.add_argument('--white-threshold', type=int, help='この値以上は白として扱う (0-255)')
    parser.add_argument('-g', '--gamma', type=float, default=1.0, help='ガンマ補正値 (1.0がデフォルト)')
    parser.add_argument('-t', '--transparent-black', action='store_true', help='透明部分を黒として扱う (デフォルトは白)')
    # トリミング機能のオプションを追加
    parser.add_argument('--trim-width', type=int, help='トリミング後の幅 (指定しない場合は元のサイズ)')
    parser.add_argument('--trim-height', type=int, help='トリミング後の高さ (指定しない場合は元のサイズ)')
    
    args = parser.parse_args()
    
    # 引数の検証
    if args.brightness < 0.0 or args.brightness > 2.0:
        print("エラー: 明るさは0.0から2.0の間で指定してください")
        sys.exit(1)
    
    if args.contrast < 0.0 or args.contrast > 2.0:
        print("エラー: コントラストは0.0から2.0の間で指定してください")
        sys.exit(1)
    
    if args.black_threshold is not None and (args.black_threshold < 0 or args.black_threshold > 255):
        print("エラー: 黒のしきい値は0から255の間で指定してください")
        sys.exit(1)
    
    if args.white_threshold is not None and (args.white_threshold < 0 or args.white_threshold > 255):
        print("エラー: 白のしきい値は0から255の間で指定してください")
        sys.exit(1)
    
    if args.gamma <= 0.0:
        print("エラー: ガンマ値は正の値で指定してください")
        sys.exit(1)
    
    # トリミングサイズのバリデーション
    if args.trim_width is not None and args.trim_width <= 0:
        print("エラー: トリミング幅は正の値で指定してください")
        sys.exit(1)
        
    if args.trim_height is not None and args.trim_height <= 0:
        print("エラー: トリミング高さは正の値で指定してください")
        sys.exit(1)
    
    # 変換実行
    success = convert_png_to_4bit_grayscale(
        args.input, 
        args.output, 
        brightness=args.brightness,
        contrast=args.contrast,
        invert=args.invert,
        black_threshold=args.black_threshold,
        white_threshold=args.white_threshold,
        gamma=args.gamma,
        transparent_white=not args.transparent_black,
        trim_width=args.trim_width,
        trim_height=args.trim_height
    )
    
    if not success:
        sys.exit(1)
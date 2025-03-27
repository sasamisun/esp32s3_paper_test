import os
import re
import argparse
import datetime
import unicodedata
from PIL import Image, ImageDraw, ImageFont
import numpy as np

# 文字の分類と縦書き時の回転情報を取得するための関数
def get_char_typography_info(char):
    """文字のタイポグラフィ情報を取得する"""
    code_point = ord(char)
    result = {
        'needs_rotation': False,
        'rotation_angle': 0,
        'is_halfwidth': False,
        'is_fullwidth': False,
        'is_no_break_start': False,
        'is_no_break_end': False,
        'char_type': 'unknown',
        'x_offset': 0,
        'y_offset': 0,
    }
    
    # East Asian Width特性を取得
    ea_width = unicodedata.east_asian_width(char)
    result['is_halfwidth'] = ea_width in ['Na', 'H']  # Narrow, Halfwidth
    result['is_fullwidth'] = ea_width in ['F', 'W']   # Fullwidth, Wide
    
    # 文字種別の判定
    # ASCII文字と拡張ラテン文字
    if 'LATIN' in unicodedata.name(char, ''):
        result['char_type'] = 'latin'
        result['needs_rotation'] = True
        result['rotation_angle'] = 90
    # 数字
    elif '0' <= char <= '9' or ('DIGIT' in unicodedata.name(char, '')):
        result['char_type'] = 'digit'
        result['needs_rotation'] = True
        result['rotation_angle'] = 90
    # ひらがな
    elif 0x3040 <= code_point <= 0x309F:
        result['char_type'] = 'hiragana'
    # カタカナ
    elif 0x30A0 <= code_point <= 0x30FF:
        result['char_type'] = 'katakana'
    # 漢字
    elif any([
        0x4E00 <= code_point <= 0x9FFF,    # CJK統合漢字
        0x3400 <= code_point <= 0x4DBF,    # CJK統合漢字拡張A
        0x20000 <= code_point <= 0x2A6DF,  # CJK統合漢字拡張B
        0x2A700 <= code_point <= 0x2B73F,  # CJK統合漢字拡張C
    ]):
        result['char_type'] = 'kanji'
    # 記号
    elif unicodedata.category(char).startswith('P') or unicodedata.category(char).startswith('S'):
        result['char_type'] = 'symbol'
        
        # 特定の記号は縦書きで回転
        rotated_symbols = '()[]{}⟨⟩《》〈〉「」『』【】〔〕（）［］｛｝"\'\'\"'
        if char in rotated_symbols:
            result['needs_rotation'] = True
            result['rotation_angle'] = 90
    
    # 禁則文字判定
    no_break_start_chars = ',.!?)]｝、。，．・：；？！゛゜ヽヾゝゞ々ー」』】〕〉》）］｝〟"' "' _ ‐ ー ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮ'
    no_break_end_chars = '([｛「『【〔〈《（［｛〝"' "'
    
    result['is_no_break_start'] = char in no_break_start_chars
    result['is_no_break_end'] = char in no_break_end_chars
    
    return result

def optimize_char_width(img):
    """文字の実際の幅を検出（余白を削除）"""
    data = np.array(img)
    if data.size == 0:
        return 0
    # 非空白ピクセルがある列を検出
    non_empty_cols = []
    for x in range(data.shape[1]):
        if np.any(data[:, x] < 255):
            non_empty_cols.append(x)
    
    if not non_empty_cols:
        return 0
    
    # 最小と最大の非空白列を取得し、幅を計算
    # 追加の余白を設けるために、右側にパディングを追加
    min_col = min(non_empty_cols)
    max_col = max(non_empty_cols)
    padding = 2  # 右側に追加するパディング
    
    return max_col - min_col + 1 + padding

def load_charset_from_file(file_path):
    """ファイルから文字セットを読み込む"""
    with open(file_path, 'r', encoding='utf-8') as f:
        return f.read().strip()

def create_char_bitmap(font, char, font_size, calculated_height, optimize_width=True, padding=4):
    """文字のビットマップを作成する共通関数"""
    # スペース文字の特別処理
    if char == ' ':  # 半角スペース
        # 半角スペースは幅を指定サイズの半分に設定
        char_width = font_size // 2
        img_width = char_width
        img_height = calculated_height
        
        # 空の画像を作成（すべて白）
        img = Image.new('1', (img_width, img_height), color=255)
        
        return img, char_width, img_width, img_height
        
    elif char == '\u3000':  # 全角スペース（U+3000）
        # 全角スペースは幅を指定サイズと同じに設定
        char_width = font_size
        img_width = char_width
        img_height = calculated_height
        
        # 空の画像を作成（すべて白）
        img = Image.new('1', (img_width, img_height), color=255)
        
        return img, char_width, img_width, img_height
    
    # テキストのサイズを取得
    bbox = font.getbbox(char)
    char_width = bbox[2] - bbox[0]
    char_height = bbox[3] - bbox[1]
    
    # 画像サイズは最大でもフォントサイズに制限するが、余裕を持たせる
    img_width = min(font_size * 2, char_width + padding)  # 横幅に余裕を持たせる
    img_height = calculated_height
    
    # 画像作成
    img = Image.new('1', (img_width, img_height), color=255)
    draw = ImageDraw.Draw(img)
    # ベースラインを考慮して文字を配置
    # 文字の左上を (padding/2, 0) から描画開始
    draw.text((padding // 2, 0), char, font=font, fill=0)
    
    # 最適化された文字幅を取得
    width = char_width
    if optimize_width:
        detected_width = optimize_char_width(img)
        if detected_width > 0:
            width = detected_width
    
    return img, width, img_width, img_height

def generate_font_header(font_path, font_size, charset, output_file, fallback_font_path=None, optimize_width=True):
    """フォントデータをCヘッダファイルに変換"""
    # 出力ファイル名から変数名を生成
    base_name = os.path.basename(output_file)
    font_var_name = os.path.splitext(base_name)[0]
    font_var_name = re.sub(r'[^a-zA-Z0-9_]', '_', font_var_name)
    if font_var_name[0].isdigit():
        font_var_name = "font_" + font_var_name
    
    chars_var_name = f"{font_var_name}_chars"
    bitmap_var_name = f"{font_var_name}_bitmap"
    
    # フォントスタイルを検出
    style = "regular"
    if "bold" in font_path.lower() or "heavy" in font_path.lower():
        style = "bold"
    elif "italic" in font_path.lower() or "oblique" in font_path.lower():
        style = "italic"
    
    try:
        font = ImageFont.truetype(font_path, font_size)
    except IOError:
        print(f"警告: {font_path} を読み込めませんでした。サポートされていないフォント形式かもしれません。")
        return False
    
    # フォールバックフォントの読み込み
    fallback_font = None
    if fallback_font_path:
        try:
            fallback_font = ImageFont.truetype(fallback_font_path, font_size)
            print(f"フォールバックフォント読み込み成功: {fallback_font_path}")
        except IOError:
            print(f"警告: フォールバックフォント {fallback_font_path} を読み込めませんでした。")
    
    # フォントのメトリクスを取得
    ascent, descent = font.getmetrics()
    actual_font_height = ascent + descent
    
    # フォントのベースライン位置を計算（アセントが文字のベースラインから上の部分）
    baseline = ascent
    
    print(f"フォント情報: サイズ={font_size}, 高さ={actual_font_height}, アセント={ascent}, ディセント={descent}, ベースライン={baseline}")
    
    char_infos = []
    bitmap_data = bytearray()
    failed_chars = []
    fallback_count = 0
    
    # 各文字の最大高さを計算
    max_height = 0
    for char in charset:
        try:
            # 主フォントで文字を確認
            char_exists = True
            try:
                bbox = font.getbbox(char)
                if bbox[2] - bbox[0] <= 0 or bbox[3] - bbox[1] <= 0:
                    char_exists = False
            except:
                char_exists = False
            
            # フォールバックフォントで確認
            if not char_exists and fallback_font:
                try:
                    bbox = fallback_font.getbbox(char)
                    if bbox[2] - bbox[0] > 0 and bbox[3] - bbox[1] > 0:
                        char_exists = True
                except:
                    pass
            
            # どちらのフォントでも文字が見つからない場合はスキップ
            if not char_exists:
                continue
                
            # 高さを計算するフォントを選択
            current_font = font
            if not char_exists and fallback_font:
                current_font = fallback_font
                
            bbox = current_font.getbbox(char)
            height = bbox[3] - bbox[1]
            max_height = max(max_height, height)
        except:
            pass
    
    # 実際のフォント高さと最大文字高さの比較、適切な余裕を持たせる
    calculated_height = min(actual_font_height, max_height + 2)
    
    print(f"最大文字高さ: {max_height}, 計算後の高さ: {calculated_height}")
    
    # 各文字をビットマップに変換
    for i, char in enumerate(charset):
        try:
            code_point = ord(char)
            
            # タイポグラフィ情報を取得
            typo_info = get_char_typography_info(char)
            
            # 主フォントで文字を確認
            char_exists = True
            try:
                bbox = font.getbbox(char)
                if bbox[2] - bbox[0] <= 0 or bbox[3] - bbox[1] <= 0:
                    char_exists = False
            except:
                char_exists = False
            
            # フォールバックフォントで確認
            use_fallback = False
            if not char_exists and fallback_font:
                try:
                    bbox = fallback_font.getbbox(char)
                    if bbox[2] - bbox[0] > 0 and bbox[3] - bbox[1] > 0:
                        char_exists = True
                        use_fallback = True
                        fallback_count += 1
                except:
                    pass
            
            # どちらのフォントでも文字が見つからない場合はスキップ
            if not char_exists:
                failed_chars.append(char)
                continue
                
            # 使用するフォントを選択
            current_font = font if not use_fallback else fallback_font
            
            # 文字のビットマップを作成
            img, width, img_width, img_height = create_char_bitmap(
                current_font, char, font_size, calculated_height, optimize_width)
            
            # バイト境界に合わせたビットマップデータ作成
            pixels = np.array(img)
            byte_width = (img_width + 7) // 8  # 8ビット境界に切り上げ
            row_bytes = bytearray()
            
            for y in range(img_height):
                for x_byte in range(byte_width):
                    byte_val = 0
                    for bit in range(8):
                        x = x_byte * 8 + bit
                        if x < img_width and y < img_height and pixels[y, x] == 0:
                            byte_val |= (1 << (7 - bit))  # MSB優先
                    row_bytes.append(byte_val)
            
            # データを追加
            data_offset = len(bitmap_data)
            bitmap_data.extend(row_bytes)
            
            # タイポグラフィ情報からフラグを生成
            typo_flags = 0
            if typo_info['needs_rotation']:
                typo_flags |= 0x01  # ビット0: 回転が必要
            if typo_info['is_halfwidth']:
                typo_flags |= 0x02  # ビット1: 半角文字
            if typo_info['is_fullwidth']:
                typo_flags |= 0x04  # ビット2: 全角文字
            if typo_info['is_no_break_start']:
                typo_flags |= 0x08  # ビット3: 行頭禁則文字
            if typo_info['is_no_break_end']:
                typo_flags |= 0x10  # ビット4: 行末禁則文字
            
            # 回転角度を取得（0, 90, 180, 270のいずれか）
            rotation = typo_info['rotation_angle'] // 90
            
            # 文字情報を記録（拡張版）
            char_infos.append((
                code_point,        # Unicode値
                width,             # 文字幅
                data_offset,       # ビットマップデータのオフセット
                img_width,         # 画像幅
                img_height,        # 画像高さ
                typo_flags,        # タイポグラフィフラグ
                rotation,          # 回転情報（0-3）
                typo_info['x_offset'],  # X方向オフセット
                typo_info['y_offset']   # Y方向オフセット
            ))
            
            if use_fallback:
                print(f"フォールバックフォントを使用: '{char}' (U+{code_point:04X})")
            
        except Exception as e:
            print(f"警告: 文字 '{char}' (U+{ord(char):04X}) の処理中にエラー: {e}")
            failed_chars.append(char)
    
    # 失敗した文字があれば報告
    if failed_chars:
        print(f"エラー: 以下の {len(failed_chars)} 文字はどちらのフォントにも見つかりませんでした:")
        for i, char in enumerate(failed_chars):
            print(f"  U+{ord(char):04X} '{char}'", end=", " if (i + 1) % 10 != 0 else "\n")
        print()
    
    # ソートして検索を高速化
    char_infos.sort(key=lambda x: x[0])  # コードポイントでソート
    
    print(f"処理結果: 成功={len(char_infos)}, フォールバック使用={fallback_count}, 失敗={len(failed_chars)}")
    
    # ヘッダーファイル生成
    with open(output_file, 'w', encoding='utf-8') as f:
        # ヘッダガード
        f.write("#pragma once\n\n")
        
        # コメント情報
        f.write("// 自動生成されたフォントデータ\n")
        f.write(f"// フォント: {os.path.basename(font_path)}, サイズ: {font_size}px, スタイル: {style}\n")
        if fallback_font_path:
            f.write(f"// フォールバックフォント: {os.path.basename(fallback_font_path)}\n")
        f.write(f"// 生成日時: {datetime.datetime.now()}\n")
        f.write(f"// 文字数: {len(char_infos)}, データサイズ: {len(bitmap_data)} バイト\n\n")
        
        # ライブラリのインクルード
        f.write("#include \"epd_text.h\"  // フォント構造体の定義\n\n")
        
        # タイポグラフィフラグの定義
        f.write("// タイポグラフィフラグの定義\n")
        f.write("#define TYPO_FLAG_NEEDS_ROTATION  0x01  // 縦書き時に回転が必要\n")
        f.write("#define TYPO_FLAG_HALFWIDTH       0x02  // 半角文字\n")
        f.write("#define TYPO_FLAG_FULLWIDTH       0x04  // 全角文字\n")
        f.write("#define TYPO_FLAG_NO_BREAK_START  0x08  // 行頭禁則文字\n")
        f.write("#define TYPO_FLAG_NO_BREAK_END    0x10  // 行末禁則文字\n\n")
        
        # 文字情報配列
        f.write(f"const FontCharInfo {chars_var_name}[] __attribute__((section(\".rodata.font\"))) = {{\n")
        for info in char_infos:
            code_point, width, offset, img_width, img_height, typo_flags, rotation, x_offset, y_offset = info
            f.write(f"    {{ 0x{code_point:04X}, {width}, {offset}U, {img_width}, {img_height}, ")
            f.write(f"{typo_flags}U, {rotation}, {x_offset}, {y_offset} }},\n")
        f.write("};\n\n")
        
        # ビットマップデータ
        f.write(f"const uint8_t {bitmap_var_name}[] __attribute__((section(\".rodata.font\"))) = {{\n    ")
        for i, byte in enumerate(bitmap_data):
            f.write(f"0x{byte:02X}, ")
            if (i + 1) % 16 == 0:
                f.write("\n    ")
        f.write("\n};\n\n")
        
        # フォント情報構造体
        f.write(f"const FontInfo {font_var_name} = {{\n")
        f.write(f"    .size = {font_size},\n")
        f.write(f"    .max_height = {calculated_height},\n")
        f.write(f"    .baseline = {baseline},\n")
        f.write(f"    .style = \"{style}\",\n")
        f.write(f"    .chars_count = {len(char_infos)},\n")
        f.write(f"    .chars = {chars_var_name},\n")
        f.write(f"    .bitmap_data = {bitmap_var_name}\n")
        f.write("};\n")
    
    print(f"フォントデータを {output_file} に保存しました。")
    print(f"文字数: {len(char_infos)}, データサイズ: {len(bitmap_data)} バイト")
    return True

def main():
    parser = argparse.ArgumentParser(description='フォント変換ツール')
    parser.add_argument('font_file', help='フォントファイルのパス')
    parser.add_argument('--size', type=int, default=16, help='フォントサイズ (デフォルト: 16)')
    parser.add_argument('--charset', help='文字セットファイルのパス')
    parser.add_argument('--output', help='出力ファイル名')
    parser.add_argument('--optimize', action='store_true', help='文字幅を最適化')
    parser.add_argument('--multiple-sizes', type=str, help='複数サイズを生成（カンマ区切り、例: 12,16,24）')
    parser.add_argument('--fallback-font', help='フォールバックフォントのパス')
    args = parser.parse_args()
    
    # 文字セット読み込み
    charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん　" # 全角スペースを追加
    if args.charset:
        try:
            charset = load_charset_from_file(args.charset)
        except Exception as e:
            print(f"警告: 文字セットファイルの読み込みに失敗しました: {e}")
            print(f"デフォルトの文字セットを使用します。")
    
    # 複数サイズ対応
    if args.multiple_sizes:
        sizes = [int(s.strip()) for s in args.multiple_sizes.split(',')]
        font_basename = os.path.splitext(os.path.basename(args.font_file))[0]
        
        for size in sizes:
            output = args.output
            if not output:
                output = f"{font_basename}_{size}.h"
            else:
                output_base = os.path.splitext(output)[0]
                output = f"{output_base}_{size}.h"
            
            generate_font_header(args.font_file, size, charset, output, args.fallback_font, args.optimize)
    else:
        # 単一サイズ処理
        output = args.output
        if not output:
            font_basename = os.path.splitext(os.path.basename(args.font_file))[0]
            output = f"{font_basename}_{args.size}.h"
        
        generate_font_header(args.font_file, args.size, charset, output, args.fallback_font, args.optimize)

if __name__ == "__main__":
    main()
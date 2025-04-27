from PIL import Image

# 画像を開く
img = Image.open('testimgful.png')

# グレースケールに変換
grayscale = img.convert('L')

# 16階調に減色（量子化）
grayscale_16 = grayscale.quantize(colors=16)

# 保存
grayscale_16.save('testimg16.png')
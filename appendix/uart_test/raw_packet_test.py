import serial
import time

# PINGコマンドパケットを構築して送信
def send_ping(ser):
    # START_MARKER, CMD_PING, LENGTH_L, LENGTH_H, CRC_L, CRC_H, END_MARKER
    packet = bytearray([0xAA, 0x01, 0x00, 0x00, 0x01, 0xFF, 0x55])
    ser.write(packet)
    print("PINGコマンド送信完了")

# メイン処理
port = "COM3"  # 使用するCOMポートに変更
ser = serial.Serial(port, 115200, timeout=1)

try:
    send_ping(ser)
    
    # 応答を読み取って16進数表示
    time.sleep(0.5)  # 応答待ち
    response = ser.read(32)  # 十分な長さ
    
    print("応答受信:")
    print(" ".join([f"{b:02X}" for b in response]))
finally:
    ser.close()
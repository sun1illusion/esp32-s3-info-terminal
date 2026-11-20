"""串口抓日志：通过 RTS 复位芯片后连续读取，用于观察启动流程、字库/OLED 初始化、崩溃回溯。

用法（在项目根目录执行）：
    python tools/serial_capture.py                  # 默认 COM11，抓 20 秒
    python tools/serial_capture.py COM4 30          # 指定串口和时长
    python tools/serial_capture.py COM11 20 | grep -i panic

为什么会自动复位：只用默认的 DTR 复位对 CH343 板子并不可靠，
这里手动拉 RTS（EN 脚拉低）再释放，能保证抓到从 ESP-ROM 开始的完整日志。
"""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM11"
DUR = int(sys.argv[2]) if len(sys.argv) > 2 else 20
BAUD = 115200

ser = serial.Serial(PORT, BAUD, timeout=0.1)

ser.setDTR(False)  # GPIO0 = 高 -> 走正常启动，不进下载模式
ser.setRTS(True)   # EN = 低 -> 芯片复位
time.sleep(0.15)
ser.reset_input_buffer()
ser.setRTS(False)  # 释放复位，开始启动

end = time.time() + DUR
try:
    while time.time() < end:
        data = ser.read(4096)
        if data:
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
finally:
    ser.close()

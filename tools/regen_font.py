"""重新生成中文字库 myfont_16.c（会覆盖原文件，自动留 .bak 备份）。

作用：把「字库已有的码点」与「源码字符串里实际用到的字符」取并集，
      用 lv_font_conv 重新生成一份完整字库，避免界面出现空白框。

用法：
    python tools/regen_font.py

生成参数必须与原有字库保持一致，否则行高/字重/占用会变：
    --font simhei.ttf --size 16 --bpp 4 --format lvgl --no-compress
"""
import glob
import os
import re
import shutil
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.path.join(ROOT, "main")
SRC = os.path.join(MAIN, "myfont_16.c")

FONT_TTF = "C:/Windows/Fonts/simhei.ttf"
LV_FONT_CONV = "C:/Users/zhuoz/.workbuddy/binaries/node/versions/22.22.2-6/lv_font_conv.cmd"

txt = open(SRC, encoding="utf-8").read()

# SPARSE_TINY：实际码点 = range_start(176) + unicode_list 里的偏移
m = re.search(r"unicode_list_1\[\]\s*=\s*\{(.*?)\};", txt, re.S)
offsets = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", m.group(1))]
existing = set(176 + v for v in offsets)


def strip_comments(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)


used = set()
for f in glob.glob(os.path.join(MAIN, "**", "*.[ch]"), recursive=True):
    if f.endswith("myfont_16.c"):
        continue
    raw = open(f, encoding="utf-8", errors="ignore").read()
    for s in re.findall(r'"((?:[^"\\\n]|\\.)*)"', strip_comments(raw)):
        for ch in s:
            if 127 < ord(ch) < 0x10000:
                used.add(ord(ch))

allcps = sorted(existing | used)
cjk = [cp for cp in allcps if cp >= 127]
added = sorted(used - existing)

print("原有 %d 个，新增 %d 个，合并后 %d 个非 ASCII 码点"
      % (len(existing), len(added), len(cjk)))
print("新增字符: %s" % "".join(chr(c) for c in added))

rng = "0x20-0x7F," + ",".join("0x%X" % cp for cp in cjk)

shutil.copy(SRC, SRC + ".bak")

cmd = [LV_FONT_CONV,
       "--font", FONT_TTF,
       "--size", "16", "--bpp", "4",
       "--format", "lvgl", "--no-compress",
       "-r", rng,
       "-o", SRC]

print("\n执行 lv_font_conv ...")
r = subprocess.run(cmd, capture_output=True, text=True, shell=True)
print("exit =", r.returncode)
if r.stdout.strip():
    print(r.stdout[-800:])
if r.stderr.strip():
    print("STDERR:", r.stderr[-800:])

if r.returncode == 0:
    print("\n完成。接下来：编译 -> 烧写 -> 看界面确认无空白框")

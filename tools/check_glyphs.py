"""字库覆盖检查（只读，不修改任何文件）。

作用：对比 myfont_16.c 里实际生成的码点，与源码字符串字面量用到的字符，
      报告「缺哪些字」「哪些字是多余的」。

为什么需要：LVGL 查不到字形时不会报错，直接把该字符渲染成空白框，
            而字库的 -r 码点清单是生成时固定下来的，新增文案很容易漏字。

用法：
    python tools/check_glyphs.py

输出解读：
    缺失 X 个 -> X > 0 就去跑 tools/regen_font.py 重新生成字库
    字库中未被使用 -> 只是冗余提示，一般可以忽略
"""
import glob
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.path.join(ROOT, "main")
SRC = os.path.join(MAIN, "myfont_16.c")

txt = open(SRC, encoding="utf-8").read()

# 字库是 SPARSE_TINY 格式：unicode_list 里存的是相对 range_start 的偏移，
# 实际码点 = range_start(176) + list 值。漏掉这个 176 会得到完全错误的结论。
m = re.search(r"unicode_list_1\[\]\s*=\s*\{(.*?)\};", txt, re.S)
offsets = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", m.group(1))]
existing = set(176 + v for v in offsets) | set(range(32, 127))


def strip_comments(s):
    """注释里的字不需要进字库，先剥掉避免误报。"""
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)


used = {}
for f in glob.glob(os.path.join(MAIN, "**", "*.[ch]"), recursive=True):
    if f.endswith("myfont_16.c"):
        continue
    raw = open(f, encoding="utf-8", errors="ignore").read()
    for s in re.findall(r'"((?:[^"\\\n]|\\.)*)"', strip_comments(raw)):
        for ch in s:
            if ord(ch) > 127:
                used.setdefault(ord(ch), set()).add(os.path.basename(f))

missing = sorted(set(used) - existing)

print("字库现有码点: %d 个（ASCII 95 + 其他 %d）" % (len(existing), len(offsets)))
print("源码用到的非 ASCII 字符: %d 个" % len(used))
print("缺失: %d 个\n" % len(missing))

for cp in missing:
    print("  U+%04X  %s   <- %s" % (cp, chr(cp), ",".join(sorted(used[cp]))[:60]))

unused = sorted(cp for cp in existing if cp > 127 and cp not in used)
print("\n字库中未被使用: %d 个" % len(unused))
print("".join(chr(c) for c in unused[:80]))

if missing:
    print("\n>>> 有缺失，请执行: python tools/regen_font.py")

"""压一下文档截图（宽 > 1280 的缩到 1280，PNG 优化），减小仓库体积。

用法：python tools/shrink_shots.py [--dry]
"""
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = os.path.join(ROOT, "docs", "screenshots")
MAX_W = 1280
DRY = "--dry" in sys.argv


def main():
    total_before = total_after = 0
    for name in sorted(os.listdir(SHOTS)):
        if not name.lower().endswith(".png"):
            continue
        path = os.path.join(SHOTS, name)
        before = os.path.getsize(path)
        total_before += before
        im = Image.open(path)
        if im.width > MAX_W:
            h = round(im.height * MAX_W / im.width)
            im = im.resize((MAX_W, h), Image.LANCZOS)
        tmp = path + ".__tmp.png"
        im.save(tmp, "PNG", optimize=True)
        after = os.path.getsize(tmp)
        if after < before:
            if DRY:
                os.remove(tmp)
            else:
                os.replace(tmp, path)
        else:
            os.remove(tmp)
            after = before
        total_after += after
        print("%-34s %7.0f KB -> %7.0f KB" % (name, before / 1024, after / 1024))
    print("-" * 60)
    print("合计：%.2f MB -> %.2f MB" % (total_before / 1048576, total_after / 1048576))


if __name__ == "__main__":
    main()

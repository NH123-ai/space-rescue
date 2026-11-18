"""把截图降采样成 ASCII 灰度图，便于在纯文本环境里粗看版面分布。

用法：python tools/shot_ascii.py _shots/ui_1920x1080.png [cols]
"""
import sys
from PIL import Image

RAMP = " .:-=+*#%@"


def main():
    path = sys.argv[1]
    cols = int(sys.argv[2]) if len(sys.argv) > 2 else 100
    im = Image.open(path).convert("L")
    w, h = im.size
    rows = max(1, int(cols * h / w * 0.5))
    im = im.resize((cols, rows))
    px = list(im.getdata())
    print("%s  %dx%d -> %dx%d" % (path, w, h, cols, rows))
    print("+" + "-" * cols + "+")
    for y in range(rows):
        line = ""
        for x in range(cols):
            v = px[y * cols + x] / 255.0
            idx = min(len(RAMP) - 1, int(v * len(RAMP) * 1.6))
            line += RAMP[idx]
        print("|" + line + "|")
    print("+" + "-" * cols + "+")

    # 附带彩色信息：统计几个关键区域的平均色（识别青色/琥珀色/绿色）
    rgb = Image.open(path).convert("RGB")
    def avg(box):
        reg = rgb.crop(box)
        data = list(reg.getdata())
        n = len(data)
        return tuple(round(sum(c[i] for c in data) / n) for i in range(3))
    W, H = rgb.size
    print("区域均值 RGB:")
    print("  顶部栏       ", avg((0, 0, W, 62)))
    print("  左侧面板     ", avg((10, 80, 310, H - 120)))
    print("  屏幕中心(场景)", avg((W // 2 - 200, H // 2 - 200, W // 2 + 200, H // 2 + 200)))
    print("  右侧面板     ", avg((W - 340, 80, W - 10, H - 120)))
    print("  底部时间轴   ", avg((W // 2 - 350, H - 110, W // 2 + 350, H - 20)))


if __name__ == "__main__":
    main()

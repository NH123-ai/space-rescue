"""用无头 Chrome 批量导出界面截图，供文档/答辩 PPT 使用。

用法：
    python tools/shot.py                 # 按默认清单导出到 docs/screenshots
    python tools/shot.py --tag main      # 追加标签，文件名带前缀
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")
OUT = os.path.join(ROOT, "docs", "screenshots")
CHROME = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
]

# (文件名, 宽, 高, URL 查询串)
SHOTS = [
    ("01_briefing", 1600, 900, ""),                       # 任务简报
    ("02_flight_40t", 1920, 1080, "?brief=0&orbit=0&t=40"),   # 巡航中段
    ("03_flight_75t", 1600, 900, "?brief=0&orbit=0&t=75"),    # 后段
    ("04_final_95t", 1920, 1080, "?brief=0&orbit=0&t=95.5"),  # 接近完成
    ("05_algo_tab", 1600, 900, "?brief=0&orbit=0&t=45&plan=3"),  # 回溯方案
]


def find_chrome():
    for p in CHROME:
        if os.path.exists(p):
            return p
    return None


def shot(chrome, name, w, h, query):
    os.makedirs(OUT, exist_ok=True)
    png = os.path.join(OUT, name + ".png")
    url = "file:///" + os.path.join(WEB, "index.html").replace("\\", "/") \
        .replace(" ", "%20") + query
    subprocess.run([chrome, "--headless=new", "--disable-gpu", "--hide-scrollbars",
                    "--no-sandbox", "--force-device-scale-factor=1",
                    "--virtual-time-budget=6000",
                    "--window-size=%d,%d" % (w, h),
                    "--screenshot=" + png, url],
                   capture_output=True, text=True)
    ok = os.path.exists(png)
    print(("  ✓ " if ok else "  ✗ ") + png)
    return ok


def main():
    chrome = find_chrome()
    if not chrome:
        print("未找到 Chrome/Edge")
        return 1
    tag = ""
    if "--tag" in sys.argv:
        tag = sys.argv[sys.argv.index("--tag") + 1] + "_"
    print("导出截图到 %s" % OUT)
    for name, w, h, q in SHOTS:
        shot(chrome, tag + name, w, h, q)
    return 0


if __name__ == "__main__":
    sys.exit(main())

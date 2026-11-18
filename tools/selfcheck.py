"""无头 Chrome 多分辨率 UI 自检（可选工具，不参与程序运行）。

流程：
  1) 读 web/index.html，注入 tools/diag.js 生成临时页 web/_selfcheck.html
  2) 用 Chrome 在若干分辨率下打开并取回自检 JSON
  3) 打印/汇总结果，并顺手导出截图到 docs/screenshots/_check_*
  4) 清理临时文件

用法：
    python tools/selfcheck.py            # 完整自检
    python tools/selfcheck.py --keep     # 保留临时页，便于手工调试
    python tools/selfcheck.py --serve    # 只生成临时页
"""
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB = os.path.join(ROOT, "web")
TOOLS = os.path.join(ROOT, "tools")
OUT = os.path.join(ROOT, "docs", "screenshots")

CHROME_CANDIDATES = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
]
VIEWPORTS = [(1920, 1080), (1440, 900), (1280, 800)]
QUERY = "?brief=0&orbit=0&t=40"


def find_chrome():
    for p in CHROME_CANDIDATES:
        if os.path.exists(p):
            return p
    return None


def build_harness():
    src = os.path.join(WEB, "index.html")
    html = open(src, encoding="utf-8").read()
    shutil.copyfile(os.path.join(TOOLS, "diag.js"), os.path.join(WEB, "_diag.js"))
    inject = ('<div id="__diag" style="display:none"></div>\n'
              '<script src="_diag.js"></script>\n</body>')
    html = html.replace("</body>", inject)
    out = os.path.join(WEB, "_selfcheck.html")
    open(out, "w", encoding="utf-8").write(html)
    return out


def cleanup():
    for p in (os.path.join(WEB, "_selfcheck.html"), os.path.join(WEB, "_diag.js")):
        if os.path.exists(p):
            os.remove(p)


def run_check(chrome, harness, w, h):
    url = "file:///" + harness.replace("\\", "/").replace(" ", "%20") + QUERY
    os.makedirs(OUT, exist_ok=True)
    png = os.path.join(OUT, "_check_%dx%d.png" % (w, h))
    cmd = [chrome, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--no-sandbox",
           "--force-device-scale-factor=1", "--virtual-time-budget=9000",
           "--window-size=%d,%d" % (w, h),
           "--screenshot=" + png, "--dump-dom", url]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    m = re.search(r"DIAG_START(.*?)DIAG_END", r.stdout or "", re.S)
    if not m:
        return None, png
    return json.loads(m.group(1)), png


def summarize(diag):
    if diag is None:
        return "  ✗ 未能取到自检数据（页面未加载或脚本报错）"
    lines = []
    errs = diag.get("errs") or []
    lines.append("  JS 错误      : %s" % ("无" if not errs else errs))
    st = diag.get("stage") or {}
    lines.append("  主画布       : %dx%d  亮像素 %.2f%%  颜色桶 %d"
                 % (st.get("w", 0), st.get("h", 0), st.get("brightPct", 0),
                    st.get("distinctBins", 0)))
    tl = diag.get("timeline") or {}
    lines.append("  时间轴       : %dx%d  亮像素 %.2f%%"
                 % (tl.get("w", 0), tl.get("h", 0), tl.get("brightPct", 0)))
    lay = diag.get("layout") or {}
    ok_layout = (not lay.get("panelLeftOverlapTimeline")
                 and not lay.get("panelRightOverlapTimeline")
                 and not lay.get("leftOutOfView")
                 and not lay.get("rightOutOfView")
                 and not lay.get("timelineOutOfView"))
    lines.append("  布局         : 中间可用 %s px，重叠/越界检查 %s"
                 % (lay.get("centerGapPx"), "通过" if ok_layout else "有问题"))
    samples = diag.get("samples") or []
    in_view = [s for s in samples if s.get("inView")]
    drawn = [s for s in samples if s.get("sm") and s["sm"]["brightPct"] > 0.5]
    lines.append("  目标采样     : %d 个在视口内，%d/%d 在预期位置检测到亮点"
                 % (len(in_view), len(drawn), len(in_view)))
    if len(in_view) < len(samples):
        lines.append("                 注意：视口外目标 %s"
                     % [s["id"] for s in samples if not s.get("inView")])
    ship = diag.get("ship")
    lines.append("  飞船采样     : %s" % ("未找到" if not ship else
                                          "屏幕(%d,%d) 亮点 %.1f%%" %
                                          (ship["x"], ship["y"], ship["sm"]["brightPct"])))
    c = diag.get("counts") or {}
    lines.append("  列表计数     : 目标 %s / 序列 %s / 方案 %s / 日志 %s"
                 % (c.get("targets"), c.get("seq"), c.get("algos"), c.get("logs")))
    ct = diag.get("contract") or {}
    if ct:
        lines.append("  数据契约     : 校验 %s 个航段，不一致 %s 个（最大误差 %.4f）"
                     % (ct.get("checked"), ct.get("mismatches"), ct.get("maxErr", 0)))
    t = diag.get("text") or {}
    lines.append("  HUD 文本     : T+%s  已获救 %s  待救援 %s  最小余量 %s  方案 %s"
                 % (t.get("clock"), t.get("statSaved"), t.get("statPending"),
                    t.get("minLife"), t.get("seqAlgo")))
    return "\n".join(lines)


def main():
    harness = build_harness()
    print("已生成临时自检页：%s" % harness)
    if "--serve" in sys.argv:
        return
    chrome = find_chrome()
    if not chrome:
        print("未找到 Chrome/Edge，跳过自动化自检（不影响交付）")
        cleanup()
        return
    all_ok = True
    for w, h in VIEWPORTS:
        diag, png = run_check(chrome, harness, w, h)
        print("\n=== %dx%d ===" % (w, h))
        print(summarize(diag))
        print("  截图          : %s" % png)
        if diag is None or (diag.get("errs") or []):
            all_ok = False
    if "--keep" not in sys.argv:
        cleanup()
    print("\n总体结论：%s" % ("通过" if all_ok else "存在需要关注的问题"))


if __name__ == "__main__":
    main()

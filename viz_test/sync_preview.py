"""从 ../web/index.html 生成 viz_test/preview.html（视觉模块调试用预览页）。

同步规则（改的是“数据从哪来”，不是 UI 本身）：
    styles.css              -> ../web/styles.css
    traces/catalog.js       -> out/trace.js            （测试台自己生成的单份数据）
    boot.js                 -> ../web/boot.js          （boot 发现数据已存在，直接加载 app.js）
这样预览页永远和正式页面共用同一套渲染代码与样式，不会出现“两套 UI”漂移。

用法：python sync_preview.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "web", "index.html")
DST = os.path.join(HERE, "preview.html")

BANNER = """<!--
  ⚠ 本文件由 sync_preview.py 自动生成，请勿手工修改。
  它是 web/index.html 的副本，只把资源路径指向 ../web/ 与 ./out/，
  用途：在只加载 viz_module.c 的测试环境里预览 3D 效果。
  重新生成：python sync_preview.py
-->
"""

REPLACEMENTS = [
    ('href="styles.css"', 'href="../web/styles.css"'),
    ('<script src="traces/catalog.js"></script>',
     '<script src="out/trace.js"></script>'),
    ('<script src="random.js"></script>', '<script src="../web/random.js"></script>'),
    ('<script src="boot.js"></script>', '<script src="../web/boot.js"></script>'),
]


def main():
    html = open(SRC, encoding="utf-8").read()

    for old, new in REPLACEMENTS:
        if old not in html:
            raise SystemExit("同步失败：index.html 里找不到 %r，结构可能已变" % old)
        html = html.replace(old, new)

    html = html.replace("<title>", "<title>[调试预览] ", 1)
    html = html.replace("<!DOCTYPE html>", "<!DOCTYPE html>\n" + BANNER, 1)

    open(DST, "w", encoding="utf-8").write(html)
    print("已生成 %s" % DST)


if __name__ == "__main__":
    main()

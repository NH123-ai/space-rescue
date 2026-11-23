# 太空救援问题

综合程序设计五的作业。空间里有 N 个待救援目标，位置随时间低速漂移，每个目标有最长生存时间，
救援队从坐标原点出发，救完不用返回。要做的事情是算出最佳救援顺序和每个目标的救援时刻，
再把整个过程做成能看懂的演示。

在线演示（点开就能玩）：<https://nh123-ai.github.io/space-rescue/>

演示页里可以拖时间轴回看、切四种救援方案对比、鼠标拖着转视角。切换场景：
`?trace=standard|tight|cluster|wide|random|max`。

![3D 救援演示](docs/screenshots/02_flight_40t.png)

## 怎么跑

网页部分不用编译，纯静态。想自己生成数据、跑完整程序，两种方式任选。

用 CLion 打开这个目录（`CMakeLists.txt` 所在层），工具链用自带的 MinGW 即可，
右上角预置了几个运行配置：`space_rescue`（跑一次出报告）、`space_rescue (--trace web)`
（导出网页数据）、`space_rescue (--test)`（跑测试用例）、`viz_test`（视觉模块测试台）。

不装 IDE 的话：

```bat
build.bat                        :: 编出 space_rescue.exe（需要 gcc）
space_rescue.exe --trace web     :: 跑一次标准场景，导出 web/trace.js
start web\index.html             :: 打开网页演示
```

想换场景重新生成：

```bat
space_rescue.exe --n 12 --scenario tight --seed 8 --trace web
```

手动编译（任意 gcc / MinGW-w64 都行）：

```bash
gcc -O2 -std=c99 -I. -Iteammate -o space_rescue main.c \
    teammate/data_module_a.c teammate/algorithm_module_b.c teammate/plan_adapter.c \
    viz_module.c -lm
```

## 命令行参数

| 参数 | 说明 |
| --- | --- |
| `--n <1..12>` | 目标数量，默认 9 |
| `--scenario <s>` | `standard` / `tight` / `cluster` / `wide` / `random` |
| `--seed <u>` | 随机种子，默认 2026，同一个种子出同一个场景 |
| `--trace [dir]` | 导出单场景的 `trace.json` + `trace.js`，默认写到 `web` |
| `--trace-all [dir]` | 批量生成演示站的六个场景 + 场景清单，默认写到 `web/traces` |
| `--test` | 跑 8 个内置测试用例 |
| `--menu` | 交互菜单，回车换场景、`+/-` 加减目标数 |
| `--no-color` | 关掉 ANSI 颜色，输出重定向到文件时用 |

## 代码结构

```
main.c                     主程序：命令行、测试用例、结果输出
teammate/data_module_a.c   组员 A 的数据模块（读取、距离、漂移）
teammate/algorithm_module_b.c  组员 B 的算法模块（最近邻贪心、全排列回溯）
teammate/plan_adapter.c    适配层：把 A/B 接到可视化，补齐 EDF、2-opt、剪枝回溯
viz_module.c               组员 C 的可视化：控制台报表、ASCII 俯视图、导出轨迹
web/                       网页演示（HTML/CSS/JS，零依赖，整个目录就是可部署的站点）
viz_test/                  视觉模块的独立测试台，只链接 viz_module.c
docs/                      说明文档和截图
```

队友交付的原件留在 `teammate/original/`，没动过。工程里编译的是集成版，
改动的地方在源码里都用「集成调整」标了注释，方便对着看改了什么。
我们早期自己写的一版放在 `reference/`，不参与编译，只作对照。

## 实现上值得一提的几处

**目标位置的模型。** 题目说位置低速变化，我们用匀速漂移 `p(t) = p0 + v·t`，
好处是任意时刻的坐标能直接算出来，不需要一步步积分，回放也不会积累误差。
组员 A 原来写的是随机抖动，那个版本不可复现，网页回放会跟算法算出来的航线对不上，
所以集成时改成按速度漂移，随机版作为对照函数保留了。

**飞行时间要解方程，不能直接除。** 目标是动的，飞过去这段时间目标也在跑，
所以到达时刻 τ 满足 `|p0 + v·(t+τ) − ship| = SPEED·τ`，展开是一个一元二次方程，
取最小非负根就行。因为目标速度（≤0.6）远小于飞船速度（10），这个方程总有唯一正根。
组员 B 原来的实现按“距离 ÷ 速度”算，在静止假设下没问题，但一加漂移就会偏，
所以统一换成了这个解析解。

**四种方案对比。** 最近邻贪心和全排列回溯来自组员 B 的实现，生存时间优先（EDF）和
2-opt 局部搜索是适配层补的对照策略。回溯是精确解，但 `n!` 涨得太快，
`n ≤ 9` 走全排列，更大的规模换成带剪枝的版本（节点有上限，超了返回当前最优）。
页面上能看到每种方案的获救数、总航程、搜索节点数和耗时。

**可视化分两层。** C 端只负责算和导数据，浏览器负责画。导出的是目标初值、速度、
每段航程的起止时刻和距离，不是一帧帧的坐标；浏览器按上面两条解析规律把画面还原出来，
所以时间轴可以随便拖、倍速、切方案，数据也很小（9 个目标大约 6 KB）。
渲染是手写的 Canvas 2D 三维投影，没引第三方库，断网也能跑。

## 测试

```bat
viz_test\build_test.bat && viz_test\viz_test.exe       :: 视觉模块，96 条断言
space_rescue.exe --test                                :: 主程序，8 个场景用例
node tools\check_site.js web                           :: 校验网页数据（航段距离是否自洽）
```

`viz_test` 有两套构建：默认只链接 `viz_module.c` 配假数据，适合单独调视觉代码；
`build_test.bat full` 会连上队友的真实模块，同一套断言再跑一遍。

## 文档

- `docs/使用指南.md`：拿到这份代码后按什么顺序用
- `docs/部署上线.md`：怎么把它发布成公开网站
- `docs/CLion配置说明.md`：工具链、运行配置、中文乱码和编译问题的排查
- `docs/screenshots/`：界面截图，答辩和报告可以直接用

## 说明和已知问题

- 网页上的数据是预先算好的，访问者只能在六个预设场景之间切换。要做到“自己填参数即时生成”，
  得把 C 算法编译成 WebAssembly，这条路还没走。
- EasyX 版（`viz_easyx_optional.c`）代码在，但这台机器没装 EasyX，没实际编译验证过。
- 回溯有节点上限，`n = 12` 时偶尔会撞到上限，此时返回的是当前找到的最好解，不保证最优。
- 场景数量最多 12 个（`MAX_TARGET_NUM`），再大穷举类算法就不现实了。

## 分工

数据模块 A、算法模块 B、可视化模块 C、主程序与测试 D，按大纲要求分四部分完成。
在线演示和网页部分由可视化模块负责，接口和数据结构在 `space_rescue.h` 里统一。

代码用 MIT 协议，见 `LICENSE`。

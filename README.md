# 太空救援问题 · 综合程序设计五

三维漂移目标的最优救援序列求解 + **Web 3D 救援过程可视化**。

> 一句话说明升级点：把组员 C 原来“控制台打表 + EasyX 二维投影”的可视化，
> 拆成 **C 端算得准（导出轨迹数据）+ 浏览器端渲染得好（可拖拽时间轴的 3D 过程动画）**
> 两层，零第三方依赖、离线可跑，同时保留 EasyX 路径以满足大纲“界面设计”知识点。

---

## 一、快速开始（Windows）

### A. 用 CLion 打开（推荐，环境已配好）

`File → Open` 选择**工程根目录** `space_rescue`（含 `CMakeLists.txt` 的那一层），
或用命令行直接拉起：

```bat
"D:\IDEs\CLion 2026.2.1\bin\clion64.exe" "D:\dshworkplace\C analyze\space_rescue"
```

CLion 会自动用自带 MinGW（GCC 15.2.0）+ Ninja 配置好，右上角已预置 7 个运行配置：

| 配置 | 作用 |
| --- | --- |
| `space_rescue` / `space_rescue (--trace web)` / `space_rescue (--test)` | 正式程序：报告 / 导出网页数据 / 跑测试用例 |
| `viz_test` / `viz_test (--demo)` / `viz_test (--emit out)` / `viz_test_full` | 视觉模块调试台：96 条断言 / 交互沙箱 / 生成预览数据 / 连真实模块 |

`Shift + F10` 运行，`Shift + F9` 调试（已有断点调试环境，见 `docs/CLion配置说明.md`）。
三个构建目标：`space_rescue`、`viz_test`、`viz_test_full`（另可开 `viz_easyx`）。

### B. 不用 IDE，纯命令行

```bat
build.bat                     :: 用 gcc 编译出 space_rescue.exe
space_rescue.exe --trace web   :: 跑一次标准场景，并导出 web/trace.js
start web\index.html           :: 浏览器观看 3D 救援演示
```

没有 gcc 也能先看界面：直接双击 `web/index.html`
（仓库里已带一份 `web/trace.js`，是 `--n 9 --scenario standard --seed 2026` 的结果）。
换场景后重新生成即可：

```bat
space_rescue.exe --n 12 --scenario tight --seed 8 --trace web
```

手动编译（任意 gcc / MinGW-w64 / VS2022 均可）：

```bash
gcc -O2 -Wall -Wextra -std=c99 -o space_rescue \
    main.c data_module.c algorithm_module.c viz_module.c -lm
```

---

## 二、命令行参数

| 参数 | 说明 |
| --- | --- |
| `--n <1..12>` | 目标数量，默认 9（`MAX_TARGET_NUM` 上限 12） |
| `--scenario <s>` | `standard` / `tight`（生存紧张）/ `cluster`（星团）/ `wide`（大尺度）/ `random` |
| `--seed <u>` | 随机种子，默认 2026；同一 seed 生成同一场景，可复现 |
| `--trace [dir]` | 导出单场景的 `trace.json` + `trace.js` 到目录（默认 `web`），供网页渲染 |
| `--trace-all [dir]` | 批量生成**公开演示站**的多场景数据 + `catalog.js`（默认 `web/traces`） |
| `--no-trace` | 不导出 |
| `--test` | 运行 8 个内置测试用例（含“必有牺牲”的极端场景） |
| `--menu` | 交互菜单：回车换场景、`0..4` 切场景、`+/-` 增减目标数 |
| `--no-color` | 关闭 ANSI 颜色（输出重定向到文件时使用） |
| `-h, --help` | 帮助 |

---

## 三、目录结构

```
space_rescue/
├─ space_rescue.h          共用头文件（v2：仅追加字段与接口，不改旧签名）
├─ data_module.c           组员 A：场景生成 / 文件读取 / 漂移模型 / 距离计算
├─ algorithm_module.c      组员 B：四种救援顺序策略 + 拦截时间解析解 + 仿真
├─ viz_module.c            组员 C：控制台仪表盘 / ASCII 俯视图 / trace 导出
├─ viz_easyx_optional.c    组员 C：EasyX 原生动画（-DEASYX 时才编译，可选）
├─ main.c                  组员 D：命令行、测试用例、结果输出、联调入口
├─ CMakeLists.txt          ★ CMake 工程（CLion 直接打开；三个目标，见下）
├─ .idea/                  ★ 已配好的 CLion 工程（工具链 / 运行配置 / 工作目录）
├─ build.bat / Makefile    不用 IDE 时的构建脚本（产物在根目录）
├─ cmake-build-debug/      CLion / CMake 的构建产物（可删，重新构建会生成）
├─ web/                    ★ 网页可视化（零依赖，可离线；整个目录就是可部署的静态站点）
│   ├─ index.html          页面结构（含场景切换下拉框）
│   ├─ styles.css          深空玻璃拟态样式
│   ├─ app.js              3D 渲染 + HUD + 时间轴 + 图表（约 1.4k 行）
│   ├─ boot.js             按 ?trace=<key> 选数据集，再加载 app.js
│   ├─ traces/             ← `--trace-all` 生成：catalog.js + 各场景 trace.js/json
│   ├─ trace.js            ← `--trace` 生成的单场景数据（旧入口，作为兜底）
│   └─ .nojekyll           GitHub Pages 用（禁用 Jekyll）
├─ .github/workflows/pages.yml   GitHub Pages 自动部署（发布 web/）
├─ netlify.toml / vercel.json    其它静态托管的一行配置
├─ tools/                  自检与截图工具（Python，非交付运行依赖）
│   ├─ gen_trace_reference.py  与 C 等价的 Python 参考实现（交叉验证用）
│   ├─ selfcheck.py            无头 Chrome 多分辨率 UI 自检
│   ├─ diag.js                 自检注入脚本（运行时复制到 web/_diag.js）
│   ├─ shot.py                 批量导出截图
│   └─ shot_ascii.py           把截图转 ASCII，便于纯文本环境看版面
├─ viz_test/               ★ 视觉模块独立测试台（只链接 viz_module.c）
│   ├─ viz_test.exe            直接运行即跑 96 条断言（编译产物，已带）
│   ├─ viz_test.c              9 组断言 + 交互沙箱 + trace 生成
│   ├─ mock_data.c/.h          假数据模块 + 手工造场景/造方案工具
│   ├─ build_test.bat          独立模式｜build_test.bat full 连真实模块联调
│   ├─ preview.html            用真实渲染器预览手工构造的测试数据
│   └─ README.md               测试覆盖清单与调试工作流
└─ docs/
    ├─ 使用指南.md              ★ 拿到文件后怎么用（最实用的入口）
    ├─ 部署上线.md              ★ 做成公开网站（GitHub Pages / Vercel / Netlify）
    ├─ 接入队友模块.md          ★ 队友代码就绪后，一条命令切到正式数据
    ├─ CLion配置说明.md         工具链、运行配置、乱码与编译排查
    ├─ 队员C可视化升级说明.md   ★ 升级方案与评分对照（写报告直接抄）
    ├─ 飞书文档_修订稿.md       ★ 可直接替换飞书里“队员 C”相关段落
    └─ screenshots/             界面截图（答辩/报告可用）
```

---

## 三·五、作为公开网站上线（任何人输网址就能玩）

整套前端是**纯静态**的（`web/` 目录就是一个站点），数据由 C 程序预先生成，
所以不需要服务器、不需要数据库，静态托管免费即可：

```bat
tools\build_site.bat      :: 自动：正式模块能编过就用它，否则退回测试台（测试桩数据）
```

脚本会自动二选一，界面上会标出来：

| 数据来源 | 何时使用 | 界面徽标 |
| --- | --- | --- |
| 正式模块 `space_rescue --trace-all` | 队友 A/B/D 已就绪 | 数据：正式模块 |
| 测试台 `viz_test --emit-all` | **队友代码还没写完（当前状态）** | 数据：测试桩（等队友模块接入） |

> 目前线上跑的是测试桩数据：方案名都带“（测试桩）”后缀，不会冒充真实算法；
> 队友交付后重跑一次 `tools\build_site.bat` 即自动切到正式数据，
> 再 `commit + push` 就上线（CI 只发布已提交的 `web/`，不在云端编译 C），
> 具体步骤与验收清单见 `docs/接入队友模块.md`。

**线上地址（已部署）**：<https://nh123-ai.github.io/space-rescue/>

然后三选一：

- **GitHub Pages**：push 到 GitHub → Settings → Pages → Source 选 *GitHub Actions*，
  仓库自带的 `.github/workflows/pages.yml` 会自动发布 `web/`；
  地址形如 `https://<用户名>.github.io/<仓库名>/?trace=tight`
- **Vercel / Cloudflare Pages**：连仓库，输出目录填 `web`，构建命令留空
- **Netlify**：`netlify.toml` 已声明发布目录，连仓库或直接把 `web` 文件夹拖进 Netlify Drop

场景用 URL 参数切换（可直接分享）：`?trace=standard|tight|cluster|wide|random|max`

> 局限：数据是预先生成的，访问者只能在预设场景间切换。
> 想做到“随便输参数即时生成”，需要把 C 算法编成 WebAssembly（或加后端），
> 方案与工作量见 `docs/部署上线.md` 第五节。

---

## 四、Web 可视化做了什么

**3D 星域场景**（Canvas 2D 手写三维投影，无 three.js 等依赖）

- 目标球体：外圈是**生存剩余环**（青 → 琥珀 → 红），中心点按状态变色，已获救打勾、已牺牲打叉
- 飞船：按航向旋转的船体 + 引擎光焰 + 拖尾轨迹，飞向目标的**探照光锥**
- 航线：已飞段实线发光、当前段琥珀高亮、未飞段虚线；每段标注序号与航程
- 场景要素：漂移尾迹、星云薄雾、亮星十字光芒、透视地面网格、XYZ 坐标轴、基站脉冲环
- 事件特效：对接成功扩散环 +“已获救”飘字、牺牲“失联”提示、每 `t=5` 的**坐标刷新扫描环**

**HUD 与交互**

- 任务时钟 T+、总时长、进度百分比；获救 / 牺牲 / 待救援 / 最小生存余量四个实时指标
- 播放控制：播放暂停、0.25×~4× 倍速、单步、回到起点
- 底部**可拖拽时间轴**：获救/牺牲刻度、坐标刷新刻度、悬停显示时刻、点击跳转
- 右侧三个面板：**目标**（生存余量条 + 状态，按“在途→待救援→已获救→牺牲”自动排序）、
  **算法对比**（总航程/搜索节点/耗时条形图，点击切换演示航线）、**事件日志**（点击跳转到该事件）
- 视角：鼠标拖拽轨道旋转、滚轮缩放、双击复位、自动环绕、俯视视角、全部方案航线叠加
- 其他：任务简报遮罩、PNG 导出、`?t=40&plan=3&brief=0` 深链接、快捷键（空格/←→/R/C）

---

## 五、算法与复杂度对比

同一次运行（`--n 9 --scenario standard --seed 2026`）四种策略的结果：

| 策略 | 获救 | 总航程 | 搜索节点 | 耗时 | 复杂度 |
| --- | --- | --- | --- | --- | --- |
| 最近邻贪心 NN | 9/9 | 955.81 | 45 | <0.01 ms | O(n²) |
| 生存时间优先 EDF | 9/9 | 1746.63 | 45 | <0.01 ms | O(n²) |
| 2-opt 局部搜索 | 9/9 | 955.81 | 126 | 0.009 ms | O(n²·轮数) |
| 回溯（剪枝） | 9/9 | 955.81 | 62 284 | 3.2 ms | 最坏 O(n!)，剪枝后 ≈O(n·2ⁿ) |

> 节点数口径统一为“所有被评估过的候选数”，含 2-opt 构解阶段（NN+EDF 共 90 次），
> 以便与其他策略横向比较复杂度。

极端场景（`--n 12 --scenario tight --seed 8`）：回溯给出 9/12（3 个目标必然牺牲），
贪心只有 8/12 —— 这正是“算法对比”面板和红色牺牲状态的演示场景。

**关键点：到达时刻是解析解而非近似**

目标漂移 `P(t)=P0+v·t`，飞船恒速 `SPEED`，拦截满足
`|P(t_begin+τ) − S| = SPEED·τ`，展开为
`(|v|²−SPEED²)τ² + 2(D·v)τ + |D|² = 0`，
因 `|v| ≪ SPEED` 该方程有唯一正根，直接求解即可，避免时间步逼近带来的累计误差。

---

## 六、数据契约（`web/trace.json`）

```jsonc
{
  "meta":   { "scenarioName": "标准场景", "seed": 2026, "count": 9,
              "speed": 10.0, "updateInterval": 5.0, "selectedPlan": 0 },
  "targets":[ { "id":0, "name":"T1", "p0":[x,y,z], "v":[vx,vy,vz],
                "life":126.57, "dist0":123.5, "eta0":12.35 } ],
  "plans":  [ { "id":0, "algo":0, "name":"最近邻贪心 NN",
                "saved":9, "dead":0, "feasible":1, "length":955.81,
                "finish":95.58, "nodes":45, "elapsedMs":0.004,
                "route":[7,3,4,1,5,8,2,6,0],
                "legs":[ {"from":-1,"to":7,"t0":0,"t1":8.67,"dist":86.70} ],
                "outcome":[ {"id":0,"state":"saved","t":95.58} ] } ],
  "events": [ { "t":0, "type":"launch|saved|dead|refresh|finish",
                "target":-1, "text":"…" } ]
}
```

前端**不逐帧接收坐标**，而是用两条解析规律还原动画：
目标 `p0 + v·t`；飞船在每段 `[t0,t1]` 内从上一落点匀速直线飞向目标在 `t1` 的位置。
因此时间轴可以任意拖拽/倍速/切换方案，数据量也小（9 目标约 6 KB）。

---

## 七、自检与验证

```bash
viz_test\build_test.bat        # 编出视觉模块独立测试台（只链接 viz_module.c）
viz_test\viz_test.exe          # 96 条断言：文本对齐/仪表盘/俯视图/过程模拟/trace 导出
python tools/selfcheck.py      # 无头 Chrome 跑 1920/1440/1280 三种分辨率
```

`viz_test.exe` 全部通过时退出码为 0，可直接接脚本；另有两种调试模式：
`--demo`（交互式改场景、立刻看输出）、`--emit out`（生成 trace 供 `viz_test/preview.html` 预览 3D 效果）。

自检内容与最近结果：JS 错误 0；9/9 目标在预期屏幕位置检测到亮点；
左右面板与时间轴无重叠、无越界；HUD 文本、目标行状态、日志条目数量正确。
`tools/gen_trace_reference.py` 是与 C 等价的独立 Python 实现，
同一个 seed 下目标坐标、救援顺序、航段时刻与 C 完全一致，用于交叉验证。

---

## 八、评分点对照（详见 `docs/队员C可视化升级说明.md`）

| 评分项 | 本项目对应内容 |
| --- | --- |
| 需求分析（10） | 场景/约束/刷新间隔建模；功能限定与理由 |
| 概要设计（8） | 四模块单向依赖、数据结构 `RescueData` / `RoutePlan`、数据流图 |
| 详细设计（8） | 每个函数接口表 + 流程图（拦截时间求解、回溯剪枝、可视化数据流） |
| 核心算法（4） | 解析拦截时间 + NN/EDF/2-opt/回溯 对比与复杂度分析 |
| 软件实现（35） | 四个 `.c` 与设计一一对应、注释规范、参数校验与边界处理 |
| 软件测试（20） | 8 个可复现测试用例 + 多分辨率 UI 自检 + 参考实现交叉验证 |
| 总结展望（5） | 局限（回溯节点上限）与优化方向（实时推送/WebGL/录屏） |
| 扩展功能 30 分 | 算法效率对比、界面及动画设计、设置初始状态并模拟演示、给出最佳方案 |

---

## 九、常见问题

- **控制台中文乱码**：已从根上解决——所有中文输出走 `viz_printf()`，按 `RESCUE_OUT_CP`
  切换输出编码（默认 UTF-8；CLion 的运行配置里已预置 `RESCUE_OUT_CP=936`，所以 IDE 里直接正常）。
  两个程序都会在开头打印当前编码模式，便于自查。详见 `docs/CLion配置说明.md` 第四节。
- **CLion 里编译不通过**：先跑 `tools\diag_build.bat`（用 CLion 自带工具链从零配置+编译，结论可读），
  再对照 `docs/CLion配置说明.md` 第五节的报错对照表（exe 被占用 / 单文件编译 / EasyX 选项 / 工具链丢失 /
  在别的目录下跑 .bat）。
- **没有编译器**：安装 MinGW-w64 或 Dev-C++；也可以只用网页版（`web/index.html` 自带示例数据）。
- **EasyX 版**：`build.bat easyx` 或 `make easyx`，需要本机已装 EasyX（CMake 里对应开关 `SPACE_RESCUE_EASYX`，
  没装时会给中文提示并跳过该目标，不影响控制台版与网页版）；EasyX 与 Web 版共用同一份 `RescueData`。
- **改了参数网页没变**：`trace.js` 是 C 生成的静态文件，重新运行 `space_rescue.exe --trace web`
  再刷新页面即可（浏览器的 `file://` 不允许 fetch，所以用 `trace.js` 而不是 `trace.json` 加载）。

# 视觉模块独立测试与调试（viz_test）

> 目的：**只加载 `viz_module.c`**，用假数据把队员 C 的每个可视化接口单独喂一遍，
> 方便改样式、改文案、排查边界问题，而不用每次都跑完整的救援求解流程。
>
> 与正式工程的关系：这里只是测试台，**不参与** `space_rescue.exe` 的构建；
> 它链接的是 `../viz_module.c` 原文件，你在这里做的验证与正式程序完全同源。

---

## 一、30 秒上手

```bat
build_test.bat            :: 编译出 viz_test.exe（只含视觉模块 + 假数据）
viz_test.exe              :: 跑全部测试，看 PASS/FAIL 汇总
```

正常输出结尾长这样：

```
  汇总：通过 96 条，失败 0 条，跳过 0 条
  结论：全部通过 ✔
```

失败时每条 ✘ 都会打印“期望值 vs 实际值”，并且 `viz_test.exe` 的退出码为 1
（可以直接接进脚本或 CI）。

想手动玩：

```bat
viz_test.exe --demo        :: 交互沙箱：按数字键改场景，立刻重印仪表盘/俯视图/过程模拟
viz_test.exe --emit out    :: 生成 out/trace.js，然后用浏览器打开 preview.html 看 3D 效果
```

---

## 二、文件清单

| 文件 | 作用 |
| --- | --- |
| `viz_test.c` | 测试主程序：9 组断言 + 交互沙箱 + trace 生成 |
| `mock_data.c` / `mock_data.h` | 假数据模块（三维距离、漂移推进）＋假 `plan_name`，外加“手工造场景/造方案”的工具函数 |
| `viz_assert.h` | 极简断言宏（`VCHECK` / `VEQ_INT` / `VEQ_STR` / `VHAS` / `VNOHS`），无第三方依赖 |
| `build_test.bat` | 一键编译；`build_test.bat full` 改为链接真实的数据/算法模块 |
| `Makefile` | 同上，命令：`make` / `make full` / `make run` / `make demo` / `make emit` |
| `preview.html` | **自动生成**的预览页（`web/index.html` 的副本，路径指向 `../web/` 与 `./out/`），用真实渲染器显示测试数据 |
| `sync_preview.py` | 重新生成 `preview.html`（`web/index.html` 改过之后跑一次） |
| `out/` | 运行时产物：各测试用例的 `trace.json/js`、`report.txt` |

---

## 三、测试覆盖了什么（9 组 96 条断言）

| # | 组 | 关键断言 |
| --- | --- | --- |
| 1 | 文本宽度与对齐 | `viz_width` 中文按 2 列计；`viz_pad_r/l` 补齐宽度；**小缓冲区不切断 UTF-8 字符**；`dstsz=1` 只写结束符 |
| 2 | 推荐方案选择 | 获救数优先 → 航程 → 耗时；完全相同取第一个；`plans=NULL` / `n_plans=0` 返回 -1 |
| 3 | 控制台仪表盘 | 报告各段标题齐全；标出 ★ 推荐；**输出到文件时不含 ANSI 颜色码**；`n_plans=0` 不崩溃也不输出算法段 |
| 4 | ASCII 俯视图 | 有基点 `@` 与获救标记 `o`；单目标恰在原点、目标全部重叠、零目标时都不产生 `nan`/`inf` |
| 5 | 过程模拟 | 输出含“已对接/任务结束”；**不修改调用者的 `RescueData`**（坐标零污染，防历史 bug 回归）；无航段、越界下标、零目标不崩溃 |
| 6 | trace 导出 | 返回 0；两个文件非空；JSON 括号配平；`meta/targets/plans/events` 齐全；**导出的是 t=0 初始坐标**；`route` 顺序、`legs` 段数、`dist` 数值正确；多级目录自动创建；不可写路径返回负数；空指针参数返回 -1 |
| 7 | 边界数据 | 空方案（`route: []` `legs: []`）；全牺牲（`state: dead`）；零目标（`targets: []`）；多方案之间逗号正确 |
| 8 | 数值格式 | `-0.0` 与 `1e-12` 都输出 `0.000`，不出现 `-0.000` |
| 9 | 规模与耗时 | 12 目标 × 4 方案导出 < 300 ms、文件 < 200 KB（信息项，不作硬性失败） |

其中 5、6 两组是为了守住两个真实踩过的坑：
- 过程模拟如果直接改 `rd` 的坐标，随后导出的 trace 会整体漂移，网页里“航线与目标对不上”；
- 目标数为 0 时数组要写成 `[]`，否则前端解析空数组会踩到格式问题。

---

## 四、两种构建模式

```bat
build_test.bat            :: 独立模式（推荐）
                          ::   viz_test.c + mock_data.c + ../viz_module.c
                          ::   → 完全不碰 A、B 两个模块，改视觉代码只重编这一个小工程

build_test.bat full       :: 联调模式
                          ::   额外链接 ../data_module.c ../algorithm_module.c
                          ::   → 验证视觉模块和真实数据结构/函数名对接无误
```

两种模式都跑同一套断言，都应当 96 条全过。
`full` 模式编译时宏 `MOCK_WITH_REAL_MODULES` 会关掉 `mock_data.c` 里那四个
与真实模块重名的替身函数（`calc_3d_distance` / `update_target_pos` /
`target_pos_at` / `plan_name`），避免重复定义。

---

## 五、调试工作流建议

1. **改样式/文案**（表格列宽、颜色、甘特字符…）
   → 改 `../viz_module.c` → `viz_test.exe` → 看第 1、3 组的断言与终端输出。
2. **试边界情形**（零目标、全牺牲、单目标、坐标重叠…）
   → `viz_test.exe --demo`，用菜单键 5/6 调目标数与种子，立刻看效果。
3. **调网页端**（3D 场景、HUD、时间轴…）
   → `viz_test.exe --emit out` → 打开 `preview.html`，
   左侧/右侧面板的数据全部来自这份“手工数据”，改起来比走完整流程快得多。
4. **确认没破坏正式工程**
   → 回到上级目录跑 `build.bat` 和 `space_rescue.exe --test`（8 个用例应全 PASS）。

---

## 六、注意

- `preview.html` 是自动生成的副本，**不要手改**；`web/index.html` 改动后执行
  `python sync_preview.py` 重新同步。
- 假数据用的拦截时间公式与算法模块一致（解一元二次方程），
  所以 `--emit` 出来的 trace 拿去网页里也不会出现航线错位
  （网页端有数据契约自检，`tools/selfcheck.py` 会报告“航段长度不一致 0 个”）。
- 测试产物都写在 `out/` 下，可以随时整个删掉。

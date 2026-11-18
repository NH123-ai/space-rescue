# 在 CLion 中开发 / 调试（环境与路径已配好）

> 工程根目录：`D:\dshworkplace\C analyze\space_rescue`
> 用 CLion 打开**这个目录**即可（不要只打开 `viz_test` 子目录，那样看不到主程序目标）。
> 本文记录已经配好的环境，以及现场要用的快捷键与常见坑。

---

## 一、已经配好的东西

| 项 | 值 | 说明 |
| --- | --- | --- |
| IDE | CLion 2026.2.1（`D:\IDEs\CLion 2026.2.1`） | 工程文件已生成，打开即可用 |
| 构建系统 | CMake（CLion 自带 3.x） | 根目录 `CMakeLists.txt` |
| 编译器 | CLion 自带 MinGW：`bin\mingw\bin\gcc.exe`（GCC 15.2.0） | 与之前 Matrix 工程同一套工具链，零手工配置 |
| 生成器 / 构建 | Ninja（CLion 自带） | 构建目录 `cmake-build-debug` |
| 调试器 | CLion 自带 gdb：`bin\gdb\win\x64\bin\gdb.exe` | 已验证可命中 `main` 断点、能显示源码行 |
| C 语言标准 | C99（`CMAKE_C_STANDARD 99`，关闭 GNU 扩展） | 与命令行 `-std=c99` 一致 |
| 告警 | `-Wall -Wextra` | 当前源码 **0 warning** |
| 构建类型 | Debug（带 `-g`，可断点调试） | `cmake-build-debug/` |

> 想改用 `D:\tools\mingw64`（GCC 16.2.0，也带 gdb）：
> `Settings → Build, Execution, Deployment → Toolchains → +` 新建一个 MinGW 工具链，
> 指向 `D:\tools\mingw64\bin\gcc.exe` / `gdb.exe`，再到
> `Settings → CMake → Debug 配置 → Toolchain` 里切换即可（源码不需要任何改动）。

### 三个构建目标（CLion 左侧 Build 面板 / 右上角下拉框里可见）

| 目标 | 组成 | 产物 |
| --- | --- | --- |
| `space_rescue` | `main.c` + `data_module.c` + `algorithm_module.c` + 视觉模块静态库 | `cmake-build-debug/space_rescue.exe` |
| `viz_test` | `viz_test/viz_test.c` + `viz_test/mock_data.c` + 视觉模块静态库 | `cmake-build-debug/viz_test.exe` |
| `viz_test_full` | 同上，额外链接真实数据/算法模块（宏 `MOCK_WITH_REAL_MODULES`） | `cmake-build-debug/viz_test_full.exe` |

`viz_module.c` 被编成静态库 `viz_module` 由三者共用——**正式程序和测试台跑的是同一份视觉代码**，
不会出现“测试通过了但正式程序是另一份实现”的错觉。
可选目标 `viz_easyx` 默认不生成，需要时用 `-DSPACE_RESCUE_EASYX=ON` 打开（本机需装 EasyX）。

### 已预置的运行/调试配置（右上角下拉框直接选）

| 配置名 | 程序参数 | trace 落到哪 | 用途 |
| --- | --- | --- | --- |
| `space_rescue` | 无 | `web\trace.js`（环境变量 `RESCUE_OUT_DIR` 指定） | 跑一次标准场景，看控制台报告 |
| `space_rescue (--trace web)` | `--trace "…\space_rescue\web"`（绝对路径） | `web\trace.js` · `web\trace.json` | 生成网页演示数据 |
| `space_rescue (--test)` | `--test --no-color` | — | 8 个测试用例 |
| `viz_test` | 无 | `viz_test\out\`（测试产物） | 96 条断言，调试视觉模块的主力 |
| `viz_test (--demo)` | `--demo` | 沙箱菜单 2 导出到 `viz_test\out\` | 交互沙箱（终端模式，可键盘输入） |
| `viz_test (--emit out)` | `--emit "…\space_rescue\viz_test\out"`（绝对路径） | `viz_test\out\trace.js` | 配 `preview.html` 预览 3D |
| `viz_test_full` | 无 | `viz_test\out\` | 连真实模块联调同一套断言 |

> **为什么用绝对路径**：CLion 的 CMake 运行配置把**构建目录** `cmake-build-debug` 当工作目录，
> 于是 `--trace web` 这类相对路径会被写到 `cmake-build-debug\web\`，网页自然找不到。
> 现在这两个配置直接写绝对路径；同时程序输出会打印**绝对路径 + 当前工作目录**，
> 任何时候都能一眼确认 trace 落在哪里（换了工程位置只需改这两个配置的参数）。

---

## trace 文件在哪（一条规则 + 一张表）

**规则**：`--trace <dir>` / `--emit <dir>` 的 `<dir>` **相对于当前工作目录**；
CLion 里当前工作目录是 `cmake-build-debug`，命令行里通常是你 `cd` 进去的目录。

| 你运行的 | 真实的文件位置 | 哪个网页读它 |
| --- | --- | --- |
| `space_rescue (--trace web)`（CLion 预置配置） | `D:\dshworkplace\C analyze\space_rescue\web\trace.js` + `trace.json` | `web\index.html` |
| `viz_test (--emit out)`（CLion 预置配置） | `D:\dshworkplace\C analyze\space_rescue\viz_test\out\trace.js` + `trace.json` | `viz_test\preview.html` |
| 命令行 `cd viz_test && viz_test.exe --emit out` | `viz_test\out\` | `viz_test\preview.html` |
| `viz_test\run_preview.bat`（双击） | `viz_test\out\` | 直接自动打开 `preview.html` |

程序运行结束时会打印类似：

```
[可视化模块] 已生成（绝对路径，直接复制到资源管理器即可）：
               JSON: D:\dshworkplace\C analyze\space_rescue\web\trace.json
               JS  : D:\dshworkplace\C analyze\space_rescue\web\trace.js
[可视化模块] 这次的工作目录是：D:\dshworkplace\C analyze\space_rescue
```

看到这两行，就不存在“找不到 trace”的问题了。

---

## 二、日常操作（快捷键）

| 操作 | 快捷键 | 说明 |
| --- | --- | --- |
| 构建当前目标 | `Ctrl + F9` | 等价 `cmake --build cmake-build-debug` |
| 运行当前配置 | `Shift + F10` | 先自动构建，再运行 |
| 调试当前配置 | `Shift + F9` | 先自动构建，再 gdb 启动 |
| 重新加载 CMake | `Ctrl + Shift + O`（工具栏 Reload CMake Project） | 改了 `CMakeLists.txt` 之后 |
| 单步 / 步入 / 跳出 | `F8` / `F7` / `Shift + F8` | 断点调试 |
| 条件断点 | 断点上右键 → Condition | 例如在 `viz_write_trace` 里 `i == 5` |
| 查看变量 | Debug 面板 Variables / 鼠标悬停 | 结构体可展开看 `Target` 各字段 |

### 调试视觉模块的推荐姿势

1. 在 `viz_module.c` 里下断点，常用位置：
   - `viz_render_report` 开头（看 `plans[]` 实际传进来什么）
   - `viz_write_trace` 里 `fopen` 前后（看导出路径、返回值）
   - `viz_choose_best` 的循环内（看推荐方案是怎么比出来的）
2. 选 `viz_test` 配置 → `Shift+F9` → 程序会在第一个断言前停下，
   单步就能看到仪表盘每行是怎么拼出来的。
3. 想只看某段逻辑，可以临时把 `viz_test.c` 里其它 `test_xxx()` 调用注释掉，
   或用 `--quiet` 只看汇总。
4. 调试过程中改代码 → `Ctrl+F9` 重新构建 → `Shift+F9` 再跑；
   CLion 会复用同一个 `cmake-build-debug`，增量编译很快。

### 在 CLion 里看网页前端

- `web/index.html`、`styles.css`、`app.js` 已通过 `target_sources` 挂到工程里，
  可以直接在 CLion 中编辑（有语法高亮），但**预览要用浏览器**：
  在项目树里右键 `web/index.html` → `Open In → Browser`，
  或先跑 `space_rescue (--trace web)` 再刷新页面。
- 视觉模块的“假数据预览”用 `viz_test (--emit out)` + `viz_test/preview.html`。

---

## 三、常见问题

| 现象 | 处理 |
| --- | --- |
| 打开后左侧只有 `web/`、看不到目标 | 确认打开的是**工程根目录**（含 `CMakeLists.txt`）；`Ctrl+Shift+O` 重新加载 CMake |
| 提示找不到 CMake / Ninja / 编译器 | 本机用的是 CLion 自带工具链，若被改动过：`Settings → Toolchains` 里把默认工具链设回 MinGW（`bin\mingw`），`Settings → CMake` 里 Toolchain 选它 |
| Run 控制台中文乱码 | `Settings → Editor → File Encodings` 全部设为 UTF-8；Run 配置里勾 `Emulate terminal in output console`；或在 `Help → Edit Custom VM Options` 加 `-Dfile.encoding=UTF-8` |
| 程序跑完窗口一闪而过 | CMakeRunConfiguration 默认就在 CLion 的 Run 工具窗里，不会闪；若用了外部控制台，取消 `Use external console` |
| 断点打不上（灰圈） | 说明编译没带调试信息：确认构建类型是 **Debug**（不是 Release/RelWithDebInfo） |
| `viz_test` 写出的 `out/` 位置不对 | 看该 Run 配置的 Working directory 是否为 `viz_test` |
| 改了 `CMakeLists.txt` 不生效 | `Ctrl+Shift+O` 重新加载，或 `File → Invalidate Caches` 后重建 |

---

## 四、中文乱码（已修：输出编码可切换）

乱码的根因是“谁来解释 UTF-8 字节”：

- **真实控制台**（cmd / Windows Terminal / 双击 exe）默认按系统码页 **936(GBK)** 解释；
- **CLion 的 Run 窗口**按 IDE 自己的设置解码，默认同样是 **936**，
  所以“程序把控制台切成 UTF-8”在 CLion 里没用 —— 它不看控制台码页。

因此本项目把输出编码做成**显式可选**，而不是靠猜：

```c
viz_console_init();                    /* main() 第一行调用 */
/* 内部：读环境变量 RESCUE_OUT_CP
 *   未设置 / 65001 → 输出 UTF-8，并把控制台切到 UTF-8（真实控制台最稳）
 *   936            → 输出 GBK（适配 CLion 等按系统码页解码的控制台）        */
所有中文输出走 viz_printf()，需要时在写入前做 UTF-8 → 目标代码页 的转换。
```

**CLion 的运行配置里已经预置了 `RESCUE_OUT_CP=936`**（8 个配置都注入了），
所以现在在 CLion 里点运行，中文直接正常，不需要改任何 IDE 设置。

| 场景 | 该怎么办 | 备注 |
| --- | --- | --- |
| CLion 里 Run / Debug | 什么都不用做（配置里已带 `RESCUE_OUT_CP=936`） | 若你新建了自己的运行配置，记得在 `Environment variables` 里加 `RESCUE_OUT_CP=936` |
| 双击 exe / cmd / Windows Terminal | 什么都不用做（默认 UTF-8 + 自动切码页） | |
| 重定向到文件 | 什么都不用做（默认 UTF-8） | `space_rescue.exe --no-color > report.txt` |
| 想统一用 UTF-8 并让 IDE 也按 UTF-8 解码 | 清空 `RESCUE_OUT_CP`，并设 `Settings → Editor → General → Console → Default Encoding = UTF-8` | 二选一即可，不必两边都改 |

**自查**：跑 `viz_test.exe`，开头会打印当前模式：

```
环境：输出编码 = 936（GBK/系统码页，适配 CLion 等 IDE 控制台）
```

所有输出字符都已改成 GBK/UTF-8 **双兼容**（`√` `×` `→` `★` `≤`），
两种模式下都不会出现 `?` 替换符；若看到 `?`，说明终端字体不支持该符号，与编码无关。

---

## 五、编译不通过时的排查顺序

先跑一次诊断脚本（用 CLion 自带的那套工具链从零配置 + 编译，结论直接可读）：

```bat
tools\diag_build.bat
```

- 若它打印 `结论：✓ 配置与编译均成功` → 源码没问题，问题在 IDE 侧，按下表处理；
- 若它也失败 → 把带 `error:` 的行贴出来（或发给我），按报错定位。

| 报错/现象 | 原因 | 处理 |
| --- | --- | --- |
| `viz_test.c: No such file or directory`（跑 .bat 时） | 在**别的目录**下调用批处理，相对路径找不到源文件 | 已在脚本里加 `cd /d "%~dp0"`；更新到最新脚本即可（双击运行也正常） |
| Build 窗口 `ninja: no work to do` | 没有源文件变动，CLion 认为无需重编 | 正常；`Tools → CMake → Reset Cache and Reload Project` 可强制重编 |
| `cannot open output file ...exe: Permission denied` | 上一次运行的 `space_rescue.exe` / `viz_test.exe` 还在运行（或被杀软/网盘占用） | 关掉运行中的程序窗口，或在任务管理器结束该进程，再 Ctrl+F9 |
| `undefined reference to 'WinMain'` 之类的链接错误 | 选中了“单文件编译”，只拿一个 `.c` 去链接 | 用左上角目标下拉选 `space_rescue` / `viz_test` 再构建，不要用右键 `Compile 'xxx.c'` |
| `graphics.h: No such file or directory` | 开了 `-DSPACE_RESCUE_EASYX=ON` 但本机没装 EasyX | 去掉该 CMake option（现在 CMakeLists 也会给中文提示并跳过该目标） |
| `No CMAKE_C_COMPILER could be found` | CMake 配置丢了工具链 | `Settings → Build,Execution,Deployment → CMake`：构建目录 `cmake-build-debug`，Toolchain 选 `MinGW`（自带） |
| 改了 `CMakeLists.txt` 不生效 | CMake 未重新加载 | `Ctrl+Shift+O`（Reload CMake Project） |
| 断点打不上（灰圈） | 编译没带调试信息 | 确认 CMake profile 的 Build type 是 **Debug** |

---

## 六、命令行等价命令（不想用 IDE 时）

```bat
:: 配置（与 CLion 使用同一套自带工具链）
"D:\IDEs\CLion 2026.2.1\bin\cmake\win\x64\bin\cmake.exe" -S . -B cmake-build-debug -G Ninja ^
    -DCMAKE_MAKE_PROGRAM="D:\IDEs\CLion 2026.2.1\bin\ninja\win\x64\ninja.exe" ^
    -DCMAKE_C_COMPILER="D:\IDEs\CLion 2026.2.1\bin\mingw\bin\gcc.exe" -DCMAKE_BUILD_TYPE=Debug

:: 构建
"D:\IDEs\CLion 2026.2.1\bin\cmake\win\x64\bin\cmake.exe" --build cmake-build-debug

:: 跑
cmake-build-debug\space_rescue.exe --trace web
cmake-build-debug\viz_test.exe
```

也可以继续用根目录的 `build.bat` / `Makefile`（那套走 `D:\tools\mingw64` 或 PATH 里的 gcc，
产物在工程根目录），两套互不干扰。

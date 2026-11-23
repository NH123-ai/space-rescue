@echo off
rem ===========================================================================
rem  生成“公开演示站”的数据（web\traces\）
rem
rem  自动选择数据来源：
rem    1) 队友 A/B/D 的模块能编过        → 用正式程序：space_rescue.exe --trace-all
rem                                         （数据标记 dataSource=full，界面显示“正式模块”）
rem    2) 还编不过（比如队友还没交付完） → 退回测试台：viz_test.exe --emit-all
rem                                         （数据标记 dataSource=mock，界面显示“测试桩”）
rem
rem  两种模式的产物格式完全一致（catalog.js + <key>/trace.js），前端不用改。
rem
rem  用法： tools\build_site.bat            自动选择
rem         tools\build_site.bat full       强制用正式模块（编不过就报错）
rem         tools\build_site.bat mock       强制用测试台数据
rem ===========================================================================
chcp 65001 >nul 2>nul
cd /d "%~dp0.."

set CL=D:\IDEs\CLion 2026.2.1
set GCC=
if exist "%CL%\bin\mingw\bin\gcc.exe" set GCC=%CL%\bin\mingw\bin\gcc.exe
if not defined GCC if exist "D:\tools\mingw64\bin\gcc.exe" set GCC=D:\tools\mingw64\bin\gcc.exe
if not defined GCC set GCC=gcc

set OUT=web\traces
set MODE=%1
set SRC_MAIN=main.c teammate\data_module_a.c teammate\algorithm_module_b.c teammate\plan_adapter.c viz_module.c

echo ============================================================
echo  生成站点数据   编译器：%GCC%
echo ============================================================

if /i "%MODE%"=="mock" goto build_mock

:: ---------- 先试正式模块（队友 A/B/D + 适配层）----------
echo [1/2] 尝试编译正式程序（space_rescue）...
"%GCC%" -std=c99 -O2 -w -I. -Iteammate -o _site_build.exe %SRC_MAIN% -lm >nul 2>nul
if errorlevel 1 goto full_failed

echo [2/2] 用正式模块生成站点数据...
"_site_build.exe" --trace-all "%OUT%"
if errorlevel 1 goto full_failed
del _site_build.exe >nul 2>nul
echo.
echo 完成：数据来自【正式模块】（界面显示“数据：正式模块”）
goto done

:full_failed
del _site_build.exe >nul 2>nul
if /i "%MODE%"=="full" goto force_full_fail
echo.
echo [提示] 正式模块还编不过（队友 A/B/D 的代码可能没交付完），改用测试台数据。

:build_mock
echo 编译测试台（viz_test）...
"%GCC%" -std=c99 -O1 -w -I. -Iviz_test -o viz_test\viz_test.exe viz_test\viz_test.c viz_test\mock_data.c viz_module.c
if errorlevel 1 goto mock_failed
echo 用测试桩生成站点数据...
viz_test\viz_test.exe --emit-all "%OUT%"
if errorlevel 1 goto mock_failed
echo.
echo 完成：数据来自【测试台 (mock)】（界面显示“数据：测试桩”）
echo 队友代码交付后，重新运行 tools\build_site.bat 会自动切到正式模块。
goto done

:force_full_fail
echo [错误] 你要求强制使用正式模块，但编译失败。请检查 main.c 与 teammate\ 下的文件。
exit /b 1

:mock_failed
echo [错误] 测试台编译或生成失败。
exit /b 1

:done
echo.
echo 自检：node tools\check_site.js web
echo 预览：在 web\ 下 python -m http.server，或直接双击 web\index.html
exit /b 0

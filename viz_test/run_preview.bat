@echo off
rem ===========================================================================
rem  双击运行：生成测试数据并用浏览器打开 preview.html
rem    1) viz_test.exe --emit out   -> 在 viz_test\out\ 下生成 trace.js / trace.json
rem    2) 用默认浏览器打开 viz_test\preview.html（它读的正是 out\trace.js）
rem ===========================================================================
chcp 65001 >nul 2>nul
cd /d "%~dp0"

set EXE=..\cmake-build-debug\viz_test.exe
if not exist "%EXE%" set EXE=.\viz_test.exe
if not exist "%EXE%" goto no_exe

echo [1/2] 生成测试数据（9 目标 / 4 方案，手工构造）...
"%EXE%" --emit out
if errorlevel 1 goto emit_fail

echo.
echo [2/2] 打开预览页 preview.html ...
start "" "%~dp0preview.html"
echo.
echo 若浏览器没自动打开，手动双击：%~dp0preview.html
echo 提示：页面里的“算法对比”面板可点不同策略切换航线；“任务简报”里点“开始演示”播放动画。
timeout /t 6 >nul
exit /b 0

:emit_fail
echo [错误] 生成 trace 失败，见上方输出。
pause
exit /b 1

:no_exe
echo [错误] 找不到 viz_test.exe。请先编译：
echo    在 CLion 里 Ctrl+F9，或双击本目录 build_test.bat
pause
exit /b 1

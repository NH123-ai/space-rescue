@echo off
rem ===========================================================================
rem  双击运行：视觉模块调试沙箱（viz_test.exe --demo）
rem  · 会自动 cd 到本目录，所以 out/ 产物会落在 viz_test\out\ 下
rem  · 优先用 CLion 构建的 cmake-build-debug\viz_test.exe，没有就退回本目录的
rem ===========================================================================
chcp 65001 >nul 2>nul
cd /d "%~dp0"

set EXE=..\cmake-build-debug\viz_test.exe
if not exist "%EXE%" set EXE=.\viz_test.exe
if not exist "%EXE%" goto no_exe

echo 使用程序：%EXE%
echo 场景数据由 mock_data.c 手工构造，完全不依赖正式工程。
echo.
"%EXE%" --demo
echo.
echo 沙箱已退出。想预览 3D 效果：先选菜单里的 2) 导出 trace，再双击 run_preview.bat
pause
exit /b 0

:no_exe
echo [错误] 找不到 viz_test.exe。请先编译：
echo    在 CLion 里按 Ctrl+F9，或双击本目录的 build_test.bat
pause
exit /b 1

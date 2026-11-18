@echo off
rem ===========================================================================
rem  太空救援问题 —— 一键构建脚本（Windows / cmd）
rem      build.bat           编译控制台 + Web 演示
rem      build.bat easyx     额外尝试编译 EasyX 图形版（需已安装 EasyX）
rem  说明：本文件为 UTF-8 编码，首行 chcp 65001 保证中文提示正常显示。
rem ===========================================================================
chcp 65001 >nul 2>nul
cd /d "%~dp0"
setlocal enabledelayedexpansion

set SRC=main.c data_module.c algorithm_module.c viz_module.c
set OUT=space_rescue.exe
set GCC=gcc

if exist "D:\tools\mingw64\bin\gcc.exe" set GCC=D:\tools\mingw64\bin\gcc.exe
if "%GCC%"=="gcc" if exist "C:\mingw64\bin\gcc.exe" set GCC=C:\mingw64\bin\gcc.exe

echo [1/3] 编译器：%GCC%
"%GCC%" -O2 -Wall -Wextra -std=c99 -o %OUT% %SRC% -lm
if errorlevel 1 goto compile_fail
echo [2/3] 编译成功：%OUT%
goto after_build

:compile_fail
echo [错误] 编译失败。请检查：是否安装 MinGW-w64 / Dev-C++，或改用 VS2022 打开工程。
echo        也可以跳过编译，直接用浏览器打开 web\index.html 观看演示。
exit /b 1

:after_build
if /i not "%1"=="easyx" goto hint
echo [3/3] 尝试编译 EasyX 图形版（需要本机已安装 EasyX）...
"%GCC%" -O2 -DEASYX -o space_rescue_easyx.exe %SRC% viz_easyx_optional.c -lm -lgraphics -lgdi32 -lwinmm -lole32 -luuid
if errorlevel 1 echo [提示] EasyX 版编译失败（多为未安装 EasyX）；不影响 Web 演示。
if not errorlevel 1 echo [3/3] EasyX 版编译成功：space_rescue_easyx.exe
goto hint

:hint
echo.
echo 下一步：
echo   控制台演示        %OUT%
echo   生成并提供网页演示 %OUT% --trace web   然后用浏览器打开 web\index.html
echo   测试用例集        %OUT% --test
echo   交互式反复演示    %OUT% --menu
endlocal

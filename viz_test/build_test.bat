@echo off
rem ===========================================================================
rem  视觉模块独立测试 —— 构建脚本
rem      build_test.bat           只编译“视觉模块 + 假数据”（推荐，隔离调试）
rem      build_test.bat full      额外链接真实的 data_module.c / algorithm_module.c
rem  产物：viz_test.exe
rem ===========================================================================
chcp 65001 >nul 2>nul
cd /d "%~dp0"
setlocal enabledelayedexpansion

set GCC=gcc
if exist "D:\tools\mingw64\bin\gcc.exe" set GCC=D:\tools\mingw64\bin\gcc.exe
if "%GCC%"=="gcc" if exist "C:\mingw64\bin\gcc.exe" set GCC=C:\mingw64\bin\gcc.exe

set CFLAGS=-std=c99 -O1 -Wall -Wextra -I..
set VIZ=..\viz_module.c

echo [1/2] 编译器：%GCC%

if /i "%1"=="full" goto build_full

echo [2/2] 模式：独立调试（只含 viz_module.c + mock_data.c）
"%GCC%" %CFLAGS% -o viz_test.exe viz_test.c mock_data.c %VIZ%
if errorlevel 1 goto fail
echo 构建成功：viz_test.exe
goto hint

:build_full
echo [2/2] 模式：连真实模块（viz_module.c + data_module.c + algorithm_module.c）
"%GCC%" %CFLAGS% -DMOCK_WITH_REAL_MODULES -o viz_test_full.exe ^
    viz_test.c mock_data.c %VIZ% ..\data_module.c ..\algorithm_module.c
if errorlevel 1 goto fail
echo 构建成功：viz_test_full.exe
goto hint

:fail
echo [错误] 编译失败，请检查上方报错。
exit /b 1

:hint
echo.
echo 用法：
echo   viz_test.exe                 跑全部测试，看 PASS/FAIL 汇总
echo   viz_test.exe --demo          交互式沙箱：改场景后立刻看输出
echo   viz_test.exe --emit out      生成 trace，用浏览器打开 preview.html 看 3D 效果
endlocal

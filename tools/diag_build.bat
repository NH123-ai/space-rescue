@echo off
rem ===========================================================================
rem  构建诊断脚本：用“CLion 自带的那套工具链”从零配置 + 编译一遍，
rem  并把结果浓缩成几行结论。当 CLion 里报“编译过不了”时，先跑这个脚本：
rem     · 若这里能跑通  -> 问题在 IDE 侧（工具链/配置项/选了错的目标）
rem     · 若这里也失败  -> 把这 20 行输出发给组员或贴进报告，即可定位
rem  用法：  tools\diag_build.bat
rem ===========================================================================
chcp 65001 >nul 2>nul
setlocal enabledelayedexpansion

set CL=D:\IDEs\CLion 2026.2.1
set CMAKE=%CL%\bin\cmake\win\x64\bin\cmake.exe
set NINJA=%CL%\bin\ninja\win\x64\ninja.exe
set GCC=%CL%\bin\mingw\bin\gcc.exe

echo ============================================================
echo  构建环境自检
echo ============================================================
if not exist "%CMAKE%" goto no_clion
if not exist "%NINJA%" goto no_clion
if not exist "%GCC%"   goto no_clion

echo CLion      : %CL%
for /f "tokens=*" %%v in ('"%GCC%" --version 2^>^&1') do if not defined GCCVER set GCCVER=%%v
echo 编译器     : %GCC%
echo              %GCCVER%
echo CMake      : %CMAKE%
echo 构建目录   : %CD%\cmake-build-debug
echo.

cd /d "%~dp0.."
echo 工程根目录 : %CD%
if not exist CMakeLists.txt goto no_cmakelists

echo.
echo ============================================================
echo  配置（Debug / Ninja）
echo ============================================================
"%CMAKE%" -S . -B cmake-build-debug -G Ninja ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_C_COMPILER="%GCC%" ^
    -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 goto cfg_fail

echo.
echo ============================================================
echo  编译全部目标
echo ============================================================
"%CMAKE%" --build cmake-build-debug
if errorlevel 1 goto build_fail

echo.
echo ============================================================
echo  结论：✓ 配置与编译均成功（问题在 CLion 侧，见下方提示）
echo ============================================================
echo  1) 确认 CLion 右上角选中的目标是 space_rescue / viz_test，而不是某个单文件
echo  2) Settings -^> Build,Execution,Deployment -^> CMake：
echo     构建目录应为 cmake-build-debug，Toolchain 选 MinGW（自带）
echo  3) 若之前勾过 -DSPACE_RESCUE_EASYX=ON（没装 EasyX），去掉它再 Reload CMake
echo  4) Ctrl+Shift+O 重新加载 CMake，然后 Ctrl+F9 构建
exit /b 0

:cfg_fail
echo.
echo [失败] CMake 配置阶段出错，请把上面最后 10 行贴出来。
exit /b 2

:build_fail
echo.
echo [失败] 编译阶段出错，请把上面带 “error:” 的行贴出来。
exit /b 3

:no_clion
echo [失败] 没找到 CLion 自带的工具链：%CL%
echo        如果改过安装路径，请把本脚本开头的 set CL=... 改成实际路径。
exit /b 4

:no_cmakelists
echo [失败] 当前目录没有 CMakeLists.txt：%CD%
echo        请把本脚本放在 <工程根目录>\tools\ 下再运行。
exit /b 5

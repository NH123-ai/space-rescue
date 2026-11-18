@echo off
rem ===========================================================================
rem  一键把本地仓库推到你的 GitHub（省得手敲 git 命令）
rem
rem  用法：
rem    1) 先在 GitHub 上建一个【空】仓库（不要勾 README/.gitignore/license）
rem    2) 双击本脚本，把仓库地址粘进来（形如 https://github.com/你/space-rescue.git）
rem    3) 等它跑完，回到仓库 Settings -> Pages -> Source 选 GitHub Actions
rem
rem  只想看会执行什么命令、不真的推送：  tools\push_to_github.bat --dry
rem ===========================================================================
chcp 65001 >nul 2>nul
cd /d "%~dp0.."

set DRY=0
if /i "%1"=="--dry" goto set_dry
if /i "%1"=="-n"    goto set_dry
goto arg_done
:set_dry
set DRY=1
shift
:arg_done
set URL=%1

echo ============================================================
echo  把 space_rescue 推到 GitHub
echo ============================================================
echo.

where git >nul 2>nul
if errorlevel 1 goto no_git

if not "%URL%"=="" goto have_url
echo 请粘贴你的 GitHub 仓库地址（形如 https://github.com/你的用户名/space-rescue.git）
echo 也可以直接回车跳过创建远程、只看命令。
set /p URL=地址: 

:have_url
if "%URL%"=="" goto show_only

echo.
echo [1/3] 设置远程 origin ...
if exist .git\refs goto has_repo
echo     （还没初始化仓库，先 git init）
if "%DRY%"=="1" echo     git init
if not "%DRY%"=="1" git init -q

:has_repo
if "%DRY%"=="1" goto dry_rest
git remote remove origin >nul 2>nul
git remote add origin "%URL%"
goto push

:dry_rest
echo     git remote remove origin
echo     git remote add origin "%URL%"

:push
echo [2/3] 提交未保存的改动（如果有）...
if "%DRY%"=="1" goto dry_commit
git add -A >nul 2>nul
git diff --cached --quiet
if errorlevel 1 git -c user.name="space-rescue" -c user.email="dev@example.com" commit -q -m "update site data"
goto push_step

:dry_commit
echo     git add -A
echo     git commit -m "update site data"

:push_step
echo [3/3] 推送 main 分支 ...
if "%DRY%"=="1" goto dry_end
git branch -M main >nul 2>nul
git push -u origin main
if errorlevel 1 goto push_fail
echo.
echo ============================================================
echo  推送成功！接下来：
echo   1) 打开仓库网页 -^> Settings -^> Pages -^> Source 选 GitHub Actions
echo   2) 等 Actions 跑绿（约 1 分钟）
echo   3) 访问：https://你的用户名.github.io/仓库名/
echo      切场景：https://你的用户名.github.io/仓库名/?trace=tight
echo ============================================================
pause
exit /b 0

:dry_end
echo     git branch -M main
echo     git push -u origin main
echo.
echo [dry] 以上是将会执行的命令，没有真的推送。
exit /b 0

:show_only
echo.
echo 当前远程： （无）
echo 手动执行这三行也可以（把 URL 换成你自己的）：
echo     git remote add origin https://github.com/你的用户名/space-rescue.git
echo     git branch -M main
echo     git push -u origin main
pause
exit /b 0

:push_fail
echo.
echo [失败] 推送被拒绝。常见原因：
echo   1) 仓库不是空的（建仓库时勾了 README）→ 先跑：git pull --rebase origin main
echo   2) 没登录 / 密码用了账号密码 → 浏览器弹窗里授权，或用 Personal Access Token
echo   3) 仓库地址写错（要以 .git 结尾，用户名/仓库名大小写要对）
echo.
echo 也可以直接把上面的报错发给组员帮你判断。
pause
exit /b 1

:no_git
echo [错误] 没找到 git 命令。
echo   装一个 Git for Windows（https://git-scm.com/download/win），
echo   或改用 GitHub Desktop 图形界面：Add repository -^> 选择本文件夹 -^> Publish repository。
pause
exit /b 2

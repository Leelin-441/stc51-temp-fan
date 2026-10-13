@echo off
setlocal enabledelayedexpansion
REM ============================================================================
REM  把本项目推送到 GitHub
REM
REM  用法：先在 GitHub 网站上建好一个空仓库（不要勾任何初始化选项），
REM        然后双击本文件，按提示粘贴仓库地址即可。
REM ============================================================================
pushd "%~dp0.."

echo.
echo ============================================================
echo   STC51 温控散热系统  --  推送到 GitHub
echo ============================================================
echo.

REM ---- 检查是否已配置远程 ----
git remote get-url origin >nul 2>&1
if errorlevel 1 goto :needurl

echo   当前远程地址：
git remote -v
echo.
set /p REDO=要改用别的地址吗？(y/N): 
if /i "!REDO!"=="y" goto :seturl
goto :push

:needurl
echo   还没有配置远程仓库地址。
echo.
:seturl
echo   请在 GitHub 上建好空仓库后，复制它的地址，形如：
echo       https://github.com/你的用户名/stc51-temp-fan.git
echo.
set /p URL=粘贴地址后回车: 
if "!URL!"=="" (
    echo   没有输入地址，退出。
    goto :end
)
git remote remove origin >nul 2>&1
git remote add origin "!URL!"
echo   已设置远程地址。
echo.

:push
echo ============================================================
echo   开始推送...（第一次会弹出浏览器让你登录 GitHub）
echo ============================================================
echo.
git push -u origin main
if errorlevel 1 (
    echo.
    echo ############ 推送失败 ############
    echo   常见原因：
    echo     1. 仓库地址贴错了
    echo     2. 建仓库时勾了 README/LICENSE，导致远程非空
    echo        -^> 解决：git push -u origin main --force
    echo     3. 网络问题，重试一次
    echo.
) else (
    echo.
    echo ############ 推送成功 ############
    echo   去 GitHub 刷新页面就能看到代码了。
    echo.
)

:end
popd
if /i not "%~1"=="nopause" pause
exit /b

@echo off
setlocal
title 推送到 GitHub  --  自动尝试所有方式
cd /d "%~dp0.."

echo.
echo ============================================================
echo   把 stc51-temp-fan 推送到 GitHub
echo   会自动依次尝试 5 种方式，哪条成功就停
echo ============================================================
echo.
echo   当前目录: %CD%
echo   远程地址:
git remote -v
echo.
echo   按任意键开始...
pause >nul
echo.

echo ---------- 方式 1/5：关闭证书吊销检查 ----------
git -c http.schannelCheckRevoke=false push -u origin main
if not errorlevel 1 goto ok
echo.

echo ---------- 方式 2/5：换成 OpenSSL 后端 ----------
git -c http.sslBackend=openssl push -u origin main
if not errorlevel 1 goto ok
echo.

echo ---------- 方式 3/5：关闭证书校验（临时方案）----------
git -c http.sslVerify=false push -u origin main
if not errorlevel 1 goto ok
echo.

echo ---------- 方式 4/5：两种一起关 ----------
git -c http.schannelCheckRevoke=false -c http.sslVerify=false push -u origin main
if not errorlevel 1 goto ok
echo.

echo ---------- 方式 5/5：OpenSSL + 关闭校验 ----------
git -c http.sslBackend=openssl -c http.sslVerify=false push -u origin main
if not errorlevel 1 goto ok
echo.

echo ############################################################
echo    5 种方式全部失败
echo ############################################################
echo.
echo    请把上面的完整输出截图给 AI 看。
echo    备选方案：用浏览器直接上传（见聊天里的说明）。
goto end

:ok
echo.
echo ############################################################
echo    推送成功！
echo ############################################################
echo.
echo    现在打开浏览器访问：
echo    https://github.com/Leelin-441/stc51-temp-fan
echo    刷新一下就能看到 55 个文件了。

:end
echo.
pause

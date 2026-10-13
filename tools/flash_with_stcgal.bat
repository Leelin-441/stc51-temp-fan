@echo off
REM ============================================================================
REM  用 stcgal 烧录固件  --  完全绕过 STC-ISP
REM
REM  为什么要有这个脚本？
REM    STC-ISP 卡在"正在检测目标单片机"时，什么信息都不给。
REM    stcgal 会把每一步都打印出来，能看清到底卡在哪。
REM
REM  用法：
REM    1) 双击本文件
REM    2) 看到 "Waiting for MCU, please cycle power" 之后，
REM       立刻给开发板断电再上电（拔插 USB 线，或按板上的电源开关/复位键）
REM
REM  换固件：把下面 HEX 那一行改成你要烧的文件即可
REM ============================================================================
setlocal
set PY=C:\Users\HP\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe
set PYTHONPATH=%~dp0..\..\.tools\stcgal\lib
set HEX=%~dp0..\hex\TempFan_4K.hex
set PORT=COM3

if not exist "%PY%" (
    echo [ERROR] 找不到 Python: %PY%
    goto :end
)
if not exist "%HEX%" (
    echo [ERROR] 找不到固件: %HEX%
    goto :end
)

echo.
echo   串口 : %PORT%
echo   固件 : %HEX%
echo.
echo ================================================================
echo   下面出现 "Waiting for MCU, please cycle power" 时，
echo   立刻给开发板断电再上电！
echo ================================================================
echo.
"%PY%" -u -m stcgal -P stc89 -p %PORT% -b 9600 -l 2400 "%HEX%"
echo.
echo ---------- 结束，按任意键关闭 ----------

:end
if /i not "%~1"=="nopause" pause
exit /b

@echo off
REM ============================================================================
REM  自检固件编译脚本  --  生成 hex\TempFan_Selftest.hex
REM
REM  和 build.bat 的唯一区别：
REM    1) 命令行加 DEFINE(SELFTEST_MODE=1) 覆盖 config.h 里的默认值
REM    2) **不编译 app.c**（主循环不跑应用层），省下 2KB 多 Flash
REM
REM  为什么必须省这 2KB？
REM    正式固件 code=6362；如果自检代码直接塞进正式固件里一起编，
REM    会达到 8628 字节，超过 STC89C52 的 8192 字节 Flash 上限，烧不进去。
REM
REM  用法：双击本文件（编译完会停住让你看结果）
REM ============================================================================
setlocal
set KEIL=D:\Keil_v5\C51\BIN
set MODS=main timer motor lcd1602 key uart ds18b20 indicator delay selftest
set OBJS=Objects\main.obj,Objects\timer.obj,Objects\motor.obj,Objects\lcd1602.obj,Objects\key.obj,Objects\uart.obj,Objects\ds18b20.obj,Objects\indicator.obj,Objects\delay.obj,Objects\selftest.obj

if not exist "%KEIL%\C51.EXE" (
    echo.
    echo [ERROR] Keil C51 compiler not found: %KEIL%\C51.EXE
    echo         Please edit the KEIL variable in this file.
    echo.
    goto :end
)

pushd "%~dp0..\firmware"
if not exist Objects mkdir Objects
if not exist "..\hex" mkdir "..\hex"
del /q Objects\*.obj Objects\*.hex Objects\*.map Objects\*.lst 2>nul

set FAILED=0
echo.
echo ============== [1/3] Compiling (SELFTEST_MODE=1, no app.c) ==============
for %%M in (%MODS%) do (
    "%KEIL%\C51.EXE" %%M.c SMALL "OPTIMIZE(8)" "INCDIR(.)" "OBJECT(Objects\%%M.obj)" "DEFINE(SELFTEST_MODE=1)"
    if errorlevel 2 set FAILED=1
)
if "%FAILED%"=="1" goto :err

echo.
echo ============== [2/3] Linking ==============
"%KEIL%\BL51.EXE" %OBJS% TO Objects\TempFan_Selftest
if errorlevel 2 goto :err

echo.
echo ============== [3/3] Making HEX ==============
"%KEIL%\OH51.EXE" Objects\TempFan_Selftest
if errorlevel 2 goto :err
copy /y "Objects\TempFan_Selftest.hex" "..\hex\TempFan_Selftest.hex" >nul

echo.
echo ================== BUILD OK ==================
echo   HEX : %CD%\..\hex\TempFan_Selftest.hex
echo.
echo   Flash it, then watch the LCD1602 and the serial output.
echo   When done, flash hex\TempFan.hex to run the real firmware.
echo.
echo   Expected: data=95  xdata=96   code=5111
echo ==============================================
popd
goto :end

:err
echo.
echo ############### BUILD FAILED ###############
echo   See the error messages above.
echo ############################################
popd

:end
if /i not "%~1"=="nopause" pause
exit /b

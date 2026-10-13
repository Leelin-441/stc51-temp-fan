@echo off
REM ============================================================================
REM  4KB 精简版固件编译脚本  --  生成 hex\TempFan_4K.hex
REM
REM  给只有 4KB Flash 的芯片用（例如 STC89C51RC，或者被 EEPROM 占走了一半的芯片）。
REM
REM  和 build.bat 的区别：
REM    * 应用层换成 app_min.c（紧凑版），**不编译 app.c**
REM    * **不编译 uart.c**（精简版没有串口）
REM    * **不编译 selftest.c**（自检版另有一份，见 build_selftest.bat）
REM
REM  用法：双击本文件
REM ============================================================================
setlocal
set KEIL=D:\Keil_v5\C51\BIN
set MODS=main app_min timer motor lcd1602 key ds18b20 indicator delay
set OBJS=Objects\main.obj,Objects\app_min.obj,Objects\timer.obj,Objects\motor.obj,Objects\lcd1602.obj,Objects\key.obj,Objects\ds18b20.obj,Objects\indicator.obj,Objects\delay.obj

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
echo ============== [1/3] Compiling (4KB LITE build) ==============
for %%M in (%MODS%) do (
    "%KEIL%\C51.EXE" %%M.c SMALL "OPTIMIZE(8)" "INCDIR(.)" "OBJECT(Objects\%%M.obj)" "DEFINE(FW_MINIMAL=1)"
    if errorlevel 2 set FAILED=1
)
if "%FAILED%"=="1" goto :err

echo.
echo ============== [2/3] Linking ==============
"%KEIL%\BL51.EXE" %OBJS% TO Objects\TempFan_4K
if errorlevel 2 goto :err

echo.
echo ============== [3/3] Making HEX ==============
"%KEIL%\OH51.EXE" Objects\TempFan_4K
if errorlevel 2 goto :err
copy /y "Objects\TempFan_4K.hex" "..\hex\TempFan_4K.hex" >nul

echo.
echo ================== BUILD OK ==================
echo   HEX : %CD%\..\hex\TempFan_4K.hex
echo.
echo   Target: code must be under 4096 bytes (4 KB flash)
echo   Lite build has NO serial port. Keys: K3 short -1C / long -5C,
echo   K4 short +1C / long +5C. Everything else works the same.
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

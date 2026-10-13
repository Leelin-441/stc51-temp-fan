@echo off
REM ============================================================================
REM  DS18B20 智能温控散热系统  --  一键编译脚本
REM
REM  用法：
REM    1) 双击本文件（编译完会停住让你看结果）
REM    2) 或在命令行运行：  tools\build.bat nopause
REM
REM  如果你的 Keil 不在 D:\Keil_v5，改下面这一行的路径即可。
REM ============================================================================
setlocal
set KEIL=D:\Keil_v5\C51\BIN
set MODS=main app timer motor lcd1602 key uart ds18b20 indicator delay selftest
set OBJS=Objects\main.obj,Objects\app.obj,Objects\timer.obj,Objects\motor.obj,Objects\lcd1602.obj,Objects\key.obj,Objects\uart.obj,Objects\ds18b20.obj,Objects\indicator.obj,Objects\delay.obj,Objects\selftest.obj

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
echo ============== [1/3] Compiling ==============
REM 注意：OPTIMIZE(8) / INCDIR(.) / OBJECT(...) 里的括号必须用双引号包起来，
REM      否则 cmd 在 for (...) 块里会把它们当成块的边界，报"此时不应有 INCDIR(.)"。
for %%M in (%MODS%) do (
    "%KEIL%\C51.EXE" %%M.c SMALL "OPTIMIZE(8)" "INCDIR(.)" "OBJECT(Objects\%%M.obj)" DEBUG
    if errorlevel 2 set FAILED=1
)
if "%FAILED%"=="1" goto :err

echo.
echo ============== [2/3] Linking ==============
REM BL51 / OH51 只要"有警告"返回码就是 1，只有真错误才是 2，
REM 所以这两步必须用 errorlevel 2 判断，用 1 会把成功的构建误判成失败。
"%KEIL%\BL51.EXE" %OBJS% TO Objects\TempFan
if errorlevel 2 goto :err

echo.
echo ============== [3/3] Making HEX ==============
"%KEIL%\OH51.EXE" Objects\TempFan
if errorlevel 2 goto :err

copy /y "Objects\TempFan.hex" "..\hex\TempFan.hex" >nul

echo.
echo ================== BUILD OK ==================
echo   HEX : %CD%\Objects\TempFan.hex
echo   Copy: %CD%\..\hex\TempFan.hex
echo.
echo   Flash with STC-ISP  (chip STC89C52RC, cold boot: click Download then power-cycle)
echo.
echo   Expected size: data=84  xdata=175  code=6362
echo   (selftest firmware is built separately by tools\build_selftest.bat)
echo     - data must stay under 128   (STC89C52 has only 128 bytes of DATA)
echo     - code must stay under 8192  (8 KB flash)
echo.
echo   "WARNING L16: UNCALLED SEGMENT" is HARMLESS - it only means some
echo   helper functions (get_ms, motor_get_duty, ...) are not called yet.
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

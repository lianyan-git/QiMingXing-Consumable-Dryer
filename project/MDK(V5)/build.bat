@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

REM ============================================================
REM  ?????? OTA ?????????Keil UV4 ????
REM  ??:
REM    build.bat         ?? Bootloader + APP
REM    build.bat clean   ????????(build/Objects/Listings/??)
REM    build.bat bl      ?? Bootloader
REM    build.bat app     ?? APP
REM ============================================================

set "UV4=D:\keil\UV4\UV4.exe"
set "PROJ=Project.uvprojx"
set /a FAILED=0

if not exist "%UV4%" (
    echo [ERROR] Keil UV4 not found: %UV4%
    echo         Please edit the UV4 path at the top of this script.
    exit /b 2
)
if not exist "%PROJ%" (
    echo [ERROR] %PROJ% not found. Run this bat inside project\MDK(V5)^\.
    exit /b 2
)

if /I "%~1"=="clean" goto :clean
if /I "%~1"=="bl"   goto :build_bl
if /I "%~1"=="app"  goto :build_app

echo.
echo ============================================================
echo  Building Bootloader target...
echo ============================================================
"%UV4%" -b "%PROJ%" -t "Bootloader" -o "build_bl.log" -j0
set "RC=%errorlevel%"
if not "!RC!"=="0" (
    echo [FAIL] Bootloader: UV4 exit code !RC!  ^(log: build_bl.log^)
    set /a FAILED+=1
) else (
    echo [OK]   Bootloader done.  ^(bin=Objects\Bootloader\dryer_bootloader.bin^)
)
echo.

:build_bl
if "%1"=="bl" (
    echo Building Bootloader target...
    "%UV4%" -b "%PROJ%" -t "Bootloader" -o "build_bl.log" -j0
    set "RC=%errorlevel%"
    if not "!RC!"=="0" ( echo [FAIL] Bootloader: exit code !RC! ) else ( echo [OK] Bootloader done. )
    exit /b %RC%
)

echo ============================================================
echo  Building APP target...
echo ============================================================
"%UV4%" -b "%PROJ%" -t "APP" -o "build_app.log" -j0
set "RC=%errorlevel%"
if not "!RC!"=="0" (
    echo [FAIL] APP: UV4 exit code !RC!  ^(log: build_app.log^)
    set /a FAILED+=1
) else (
    echo [OK]   APP done.  ^(bin=Objects\APP\dryer_app.bin, hex=Project.hex^)
)
echo.

if %FAILED% GTR 0 (
    echo ============================================================
    echo  BUILD FINISHED WITH ERRORS   ^(%FAILED% target(s) failed^)
    echo ============================================================
    exit /b 1
) else (
    echo ============================================================
    echo  BUILD SUCCESS
    echo    Bootloader: Objects\Bootloader\dryer_bootloader.bin
    echo    APP       : Objects\APP\dryer_app.bin
    echo  OTA ?????? app ? .bin ???
    echo ============================================================
    exit /b 0
)
goto :eof

:build_app
echo Building APP target...
"%UV4%" -b "%PROJ%" -t "APP" -o "build_app.log" -j0
set "RC=%errorlevel%"
if not "!RC!"=="0" ( echo [FAIL] APP: exit code !RC! ) else ( echo [OK] APP done. bin=Objects\APP\dryer_app.bin )
exit /b %RC%

:clean
echo Cleaning build outputs...
if exist build      rmdir /s /q build
if exist Objects    rmdir /s /q Objects
if exist Listings   rmdir /s /q Listings
if exist build_bl.log  del /q build_bl.log
if exist build_app.log del /q build_app.log
echo Done. Build outputs removed.
exit /b 0
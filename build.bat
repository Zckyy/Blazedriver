@echo off
:: ─────────────────────────────────────────────────────────────────────────────
::  build.bat — Direct MSVC build for BlazeDriver.sys
::  No MSBuild / WDK project system — calls cl.exe and link.exe directly.
::  Output: KernelDriver\out\BlazeDriver.sys
::  Load:   run load.bat as Administrator
:: ─────────────────────────────────────────────────────────────────────────────
setlocal

:: ── Paths ─────────────────────────────────────────────────────────────────────
set "MSVCVER=14.51.36231"
set "MSVCROOT=C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Tools\MSVC\%MSVCVER%"
set "CL_EXE=%MSVCROOT%\bin\Hostx64\x64\cl.exe"
set "LINK_EXE=%MSVCROOT%\bin\Hostx64\x64\link.exe"

set "WDKROOT=C:\Program Files (x86)\Windows Kits\10"
set "WDKVER=10.0.28000.0"

:: MSVC standard headers (excpt.h, stddef.h etc. — required by wdm.h)
set "INC_MSVC=%MSVCROOT%\include"
:: WDK kernel-mode headers
set "INC_KM=%WDKROOT%\Include\%WDKVER%\km"
set "INC_SH=%WDKROOT%\Include\%WDKVER%\shared"
set "INC_UM=%WDKROOT%\Include\%WDKVER%\um"
:: UCRT headers (ctype.h, etc. — required by ntdef.h)
set "INC_UCRT=%WDKROOT%\Include\%WDKVER%\ucrt"

:: WDK kernel-mode libs
set "LIB_KM=%WDKROOT%\Lib\%WDKVER%\km\x64"

:: Source and output
set "SRC=%~dp0Driver.c"
set "OUT=%~dp0out"
if not exist "%OUT%" mkdir "%OUT%"
set "OBJ=%OUT%\Driver.obj"
set "SYS=%OUT%\BlazeDriver.sys"

echo ============================================================
echo  BlazeDriver Kernel Driver ^| Direct MSVC Build
echo  Compiler : "%CL_EXE%"
echo  WDK      : %WDKVER%
echo  Output   : "%SYS%"
echo ============================================================
echo.

:: ── Step 1: Compile ──────────────────────────────────────────────────────────
echo [1/2] Compiling Driver.c...

"%CL_EXE%" ^
    /c ^
    /kernel ^
    /W3 /WX- ^
    /O2 ^
    /GS ^
    /Zi ^
    /D "_AMD64_" ^
    /D "NDEBUG" ^
    /I "%INC_MSVC%" ^
    /I "%INC_KM%" ^
    /I "%INC_SH%" ^
    /I "%INC_UM%" ^
    /I "%INC_UCRT%" ^
    /Fo"%OBJ%" ^
    "%SRC%"

if %errorlevel% neq 0 (
    echo.
    echo [!] Compile FAILED ^(see errors above^).
    pause & exit /b 1
)
echo [+] Compile OK
echo.

:: ── Step 2: Link ─────────────────────────────────────────────────────────────
echo [2/2] Linking BlazeDriver.sys...

"%LINK_EXE%" ^
    /NODEFAULTLIB ^
    /ENTRY:GsDriverEntry ^
    /SUBSYSTEM:NATIVE ^
    /DRIVER:WDM ^
    /KERNEL ^
    /RELEASE ^
    /NOLOGO ^
    /OUT:"%SYS%" ^
    "%OBJ%" ^
    "%LIB_KM%\ntoskrnl.lib" ^
    "%LIB_KM%\hal.lib" ^
    "%LIB_KM%\wmilib.lib" ^
    "%LIB_KM%\BufferOverflowK.lib"

if %errorlevel% neq 0 (
    echo.
    echo [!] Link FAILED ^(see errors above^).
    pause & exit /b 1
)

echo.
echo ============================================================
echo  [+] SUCCESS   ^>^>   %SYS%
echo ============================================================
echo.
echo  Run load.bat as Administrator to map via KDMapper.
echo.
:: Only pause if we're not running completely headless
if /i "%~1" neq "/nopause" pause

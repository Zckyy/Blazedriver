@echo off
:: ─────────────────────────────────────────────────────────────────────────────
::  load.bat — Load BlazeDriver via KDMapper
::  Run as Administrator
:: ─────────────────────────────────────────────────────────────────────────────
::
::  KDMapper uses iqvw64e.sys (Intel's vulnerable Network Adapter Diagnostic
::  Driver) to map your unsigned driver directly into kernel memory, bypassing
::  DSE (Driver Signature Enforcement).
::
::  Requirements:
::    • Run as Administrator
::    • Secure Boot DISABLED in UEFI (KDMapper's Intel driver cannot load with
::      Secure Boot on, since iqvw64e.sys would be blocked by HVCI/UEFI SB)
::    • No test signing or EV certificate needed
::
::  KDMapper binary: C:\reverse-engineering\kdmapper\x64\Release\kdmapper_Release.exe
::    BlazeDriver.sys  — built from KernelDriver\KernelDriver.vcxproj (x64 Release)
::
::  How it works:
::    1. KDMapper loads iqvw64e.sys (its own bundled copy) to get kernel R/W
::    2. Manually maps BlazeDriver.sys into kernel memory (no registry entry)
::    3. Calls BlazeDriver's DriverEntry — which calls IoCreateDriver → DriverMain
::    4. DriverMain creates \\Device\\BlazeDriver + \\DosDevices\\BlazeDriver
::    5. JustExt.exe can now open \\.\BlazeDriver and use DeviceIoControl
:: ─────────────────────────────────────────────────────────────────────────────

setlocal

set KDMAPPER=C:\reverse-engineering\kdmapper\x64\Release\kdmapper_Release.exe
set SYS_PATH=%~dp0out\BlazeDriver.sys

:: ── Validate prerequisites ────────────────────────────────────────────────────
if not exist "%KDMAPPER%" (
    echo [!] kdmapper_Release.exe not found at:
    echo     %KDMAPPER%
    echo     Check that C:\reverse-engineering\kdmapper has been built ^(x64 Release^).
    pause & exit /b 1
)

if not exist "%SYS_PATH%" (
    echo [!] BlazeDriver.sys not found at: %SYS_PATH%
    echo     Build KernelDriver\KernelDriver.vcxproj ^(x64 Release^) first.
    pause & exit /b 1
)

:: ── Run KDMapper ──────────────────────────────────────────────────────────────
echo [*] Mapping BlazeDriver.sys into kernel via KDMapper...
echo     Driver : %SYS_PATH%
echo.

"%KDMAPPER%" "%SYS_PATH%"

if %errorlevel% neq 0 (
    echo.
    echo [!] KDMapper exited with error %errorlevel%.
    echo     Common causes:
    echo       - Secure Boot is ON in UEFI  ^(must be OFF^)
    echo       - Not running as Administrator
    echo       - iqvw64e.sys already loaded by another process ^(reboot to clear^)
    echo       - Windows Defender blocked kdmapper.exe or iqvw64e.sys
    echo         ^(add both to Defender exclusions, or temporarily disable^)
    pause & exit /b 1
)

echo.
echo [+] KDMapper finished. BlazeDriver should now be active.
echo     You can verify with DebugView ^(Capture Kernel^) — look for "BlazeDriver:"
echo     Now launch JustExt.exe.
pause

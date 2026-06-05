@echo off
:: ─────────────────────────────────────────────────────────────────────────────
::  uninstall.bat — BlazeDriver Kernel Driver Uninstaller
::  Run as Administrator
:: ─────────────────────────────────────────────────────────────────────────────

setlocal

echo [*] Stopping BlazeDriver...
sc stop BlazeDriver
timeout /t 1 /nobreak > nul

echo [*] Removing BlazeDriver service...
sc delete BlazeDriver
if %errorlevel% neq 0 (
    echo [!] Could not delete service. It may already be gone.
) else (
    echo [+] BlazeDriver removed.
)

pause

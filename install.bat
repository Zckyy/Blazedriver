@echo off
:: ─────────────────────────────────────────────────────────────────────────────
::  install.bat — BlazeDriver Kernel Driver Installer
::  Run as Administrator
:: ─────────────────────────────────────────────────────────────────────────────
:: Requirements:
::   • Run from the KernelDriver\ directory (or update SYS_PATH below)
::   • Test-signing must be enabled:
::       bcdedit /set testsigning on
::       (then reboot once)
::   • The .sys file must already be built: x64\Release\BlazeDriver.sys
:: ─────────────────────────────────────────────────────────────────────────────

setlocal

:: Path to the compiled .sys — update if you build to a different output dir
set SYS_PATH=%~dp0..\x64\Release\BlazeDriver.sys

if not exist "%SYS_PATH%" (
    echo [!] BlazeDriver.sys not found at: %SYS_PATH%
    echo     Build the KernelDriver project first ^(x64 Release^).
    pause
    exit /b 1
)

echo [*] Installing BlazeDriver...
sc create BlazeDriver type= kernel start= demand binPath= "%SYS_PATH%" DisplayName= "BlazeDriver"
if %errorlevel% neq 0 (
    echo [!] sc create failed. Driver may already be registered — trying to start anyway.
)

echo [*] Starting BlazeDriver...
sc start BlazeDriver
if %errorlevel% neq 0 (
    echo [!] sc start failed. Check: 1) test signing on, 2) .sys is signed/test-signed
    pause
    exit /b 1
)

echo [+] BlazeDriver started successfully.
sc query BlazeDriver
pause

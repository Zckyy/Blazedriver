@echo off
:: ─────────────────────────────────────────────────────────────────────────────
::  unload.bat — Cleanly remove the BlazeDriver device at runtime
::  (without rebooting)
::
::  Because KDMapper doesn't register a service, sc stop won't work.
::  Instead we send IOCTL_UNLOAD to the driver itself, which deletes
::  its device object and symbolic link — making it inaccessible.
::  The driver code remains in kernel memory until reboot.
::
::  Alternatively, just reboot — the driver is fully gone after a reboot.
:: ─────────────────────────────────────────────────────────────────────────────

setlocal

:: A tiny C program to send IOCTL_UNLOAD is cleaner, but for simplicity
:: we use PowerShell's PInvoke to call DeviceIoControl.

echo [*] Sending IOCTL_UNLOAD to BlazeDriver...

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
"$sig = '[DllImport(\"kernel32.dll\",SetLastError=true)]public static extern IntPtr CreateFile(string n,uint a,uint s,IntPtr sec,uint c,uint f,IntPtr h);[DllImport(\"kernel32.dll\",SetLastError=true)]public static extern bool DeviceIoControl(IntPtr h,uint io,IntPtr i,uint il,IntPtr o,uint ol,ref uint r,IntPtr ov);[DllImport(\"kernel32.dll\")]public static extern bool CloseHandle(IntPtr h);'; $t=Add-Type -MemberDefinition $sig -Name K32 -Namespace W -PassThru; $h=$t::CreateFile('\\\\.\\\BlazeDriver',0xC0000000,3,[IntPtr]::Zero,3,0,[IntPtr]::Zero); if($h.ToInt64() -eq -1){Write-Host '[!] Could not open BlazeDriver — is it loaded?';exit 1}; $ioctl=0x0022260C+0x10; $r=0; $t::DeviceIoControl($h,$ioctl,[IntPtr]::Zero,0,[IntPtr]::Zero,0,[ref]$r,[IntPtr]::Zero)|Out-Null; $t::CloseHandle($h)|Out-Null; Write-Host '[+] IOCTL_UNLOAD sent. Device removed (driver code stays until reboot).'"

echo.
echo [i] To fully remove: just reboot.
pause

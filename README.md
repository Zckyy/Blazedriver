# Blazedriver (BlazeDriver)

A Windows x64 kernel-mode driver for cross-process memory access. Loaded via [KDMapper](https://github.com/TheCruZ/kdmapper) — no test signing or EV certificate required.

---

## Overview

BlazeDriver maps directly into kernel memory using KDMapper's `iqvw64e.sys` exploit. Because KDMapper calls `DriverEntry` with `(NULL, NULL)`, the driver delegates to `IoCreateDriver` to obtain a valid `DRIVER_OBJECT`, from which it creates a named device and symbolic link accessible from user mode.

Communication is handled via `DeviceIoControl` over the `\\.\BlazeDriver` device using `METHOD_BUFFERED` IOCTLs.

---

## Features

- **Cross-process memory read** — copies a byte range from a target process's virtual address space into the caller's buffer via `MmCopyVirtualMemory`
- **Cross-process memory write** — copies a byte range from the caller's buffer into a target process's virtual address space
- **Module base resolution** — retrieves a process's `PEB.ImageBaseAddress` via `ZwQueryInformationProcess` + PEB walking
- **Runtime device cleanup** — optional IOCTL removes the device/symlink without a reboot (driver code remains mapped until next boot)

---

## IOCTL Reference

All IOCTLs use `METHOD_BUFFERED` and `FILE_SPECIAL_ACCESS`. The base code is `0xC39`.

| IOCTL | Code | Input / Output |
|---|---|---|
| `IOCTL_READ_MEMORY` | `0xC3A` | `MEMORY_REQUEST` in/out — copies `size` bytes from `processId:address` into `cheatId:buffer` |
| `IOCTL_WRITE_MEMORY` | `0xC3B` | `MEMORY_REQUEST` in/out — copies `size` bytes from `cheatId:buffer` into `processId:address` |
| `IOCTL_GET_MODULE` | `0xC3C` | `MODULE_REQUEST` in/out — returns `PEB.ImageBaseAddress` for `processId` |
| `IOCTL_UNLOAD` | `0xC3D` | No buffer — deletes device object and symbolic link |

### Structures

```c
typedef struct _MEMORY_REQUEST {
    ULONG   processId;   // Target process PID
    ULONG   cheatId;     // Caller process PID
    UINT64  address;     // Address in target process VA space
    UINT64  buffer;      // Address in caller process VA space
    ULONG   size;        // Byte count (max 64 MB)
    UINT64  response;    // OUT: bytes copied
} MEMORY_REQUEST;

typedef struct _MODULE_REQUEST {
    ULONG   processId;
    UINT64  baseAddress; // OUT: PEB.ImageBaseAddress
} MODULE_REQUEST;
```

---

## Building

Requires Visual Studio (MSVC) and the WDK. Update the version paths in `build.bat` if your toolchain differs, then run:

```bat
build.bat
```

Output: `out\BlazeDriver.sys`

The build uses `cl.exe` and `link.exe` directly — no MSBuild or WDK project system. Compiler flags: `/kernel /O2 /GS /Zi`. Linker entry point: `GsDriverEntry`.

---

## Loading

### Via KDMapper (recommended — no test signing required)

```bat
load.bat
```

Or manually:

```
kdmapper.exe out\BlazeDriver.sys
```

### Via Service (requires test signing)

Enable test signing once and reboot:

```bat
bcdedit /set testsigning on
```

Then install and start:

```bat
install.bat   # Run as Administrator
```

---

## Unloading

KDMapper-loaded drivers cannot be stopped via `sc stop` — the driver code remains in kernel memory until the next reboot. To clean up the device and symbolic link at runtime without rebooting, send `IOCTL_UNLOAD`:

```bat
unload.bat
```

---

## Debugging

- **DebugView** (Sysinternals): enable *Capture Kernel*, filter on `BlazeDriver:`
- **WinDbg**: attach over KD pipe, USB, or network for live kernel debugging

---

## Device / Symlink

| Name | Path |
|---|---|
| Device | `\Device\BlazeDriver` |
| Symbolic link | `\\.\BlazeDriver` (`\??\BlazeDriver`) |

---

## Requirements

- Windows 10 / 11 x64
- KDMapper (for signatureless loading) **or** test-signing enabled (for service-based loading)
- WDK 10.0.28000.0 + MSVC 14.x for building

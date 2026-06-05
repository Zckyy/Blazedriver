#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  BlazeDriver — Kernel Driver Internal Header
//  Loaded via KDMapper — no test signing / EV certificate required.
// ─────────────────────────────────────────────────────────────────────────────

#include <ntifs.h>
#include <ntddk.h>
#include <wdm.h>

// ── IOCTL codes (mirror of ..\shared.h, no <Windows.h> dependency) ────────────

#define IOCTL_BASE          0xC39

#define IOCTL_READ_MEMORY   CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_BASE + 1, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_WRITE_MEMORY  CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_BASE + 2, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#define IOCTL_GET_MODULE    CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_BASE + 3, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
// Optional: remove device/symlink at runtime without rebooting.
// Usermode: DeviceIoControl(hDriver, IOCTL_UNLOAD, NULL, 0, NULL, 0, &ret, NULL)
#define IOCTL_UNLOAD        CTL_CODE(FILE_DEVICE_UNKNOWN, IOCTL_BASE + 4, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)

// ── Shared request structures ─────────────────────────────────────────────────

typedef struct _MEMORY_REQUEST {
    ULONG   processId;   // Game PID
    ULONG   cheatId;     // Cheat PID (destination for reads, source for writes)
    UINT64  address;     // Source/destination address in game process VA space
    UINT64  buffer;      // Destination/source address in cheat process VA space
    ULONG   size;        // Bytes to copy
    UINT64  response;    // OUT: bytes copied / NTSTATUS
} MEMORY_REQUEST, *PMEMORY_REQUEST;

typedef struct _MODULE_REQUEST {
    ULONG   processId;
    UINT64  baseAddress; // OUT: PEB ImageBaseAddress
} MODULE_REQUEST, *PMODULE_REQUEST;

// ── Device / Symlink names ────────────────────────────────────────────────────
#define DEVICE_NAME     L"\\Device\\BlazeDriver"
#define SYMLINK_NAME    L"\\??\\BlazeDriver"

// ── Internal helpers ──────────────────────────────────────────────────────────

NTSTATUS
KernelCopyMemory(
    _In_  PEPROCESS   SourceProcess,
    _In_  PVOID       SourceAddress,
    _In_  PEPROCESS   TargetProcess,
    _Out_ PVOID       TargetAddress,
    _In_  SIZE_T      Size,
    _Out_ PSIZE_T     BytesCopied
);

UINT64
GetProcessModuleBase(
    _In_ ULONG ProcessId
);

// ── IRP Dispatch / DriverMain / DriverEntry ───────────────────────────────────

DRIVER_INITIALIZE  DriverMain;          // Called by IoCreateDriver with real DRIVER_OBJECT
DRIVER_INITIALIZE  DriverEntry;         // Called by KDMapper with (NULL, NULL)
DRIVER_DISPATCH    IrpCreate;
DRIVER_DISPATCH    IrpClose;
DRIVER_DISPATCH    IrpDeviceControl;
DRIVER_UNLOAD      DriverUnloadRoutine;

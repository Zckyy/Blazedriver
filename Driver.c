/*
 * BlazeDriver — KDMapper-Compatible Kernel Mode Memory Driver
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * Loading: via KDMapper (uses iqvw64e.sys to map directly into kernel memory)
 *   kdmapper.exe BlazeDriver.sys
 *
 * KDMapper calls DriverEntry with (NULL, NULL) — we cannot call IoCreateDevice
 * directly because it needs a valid DRIVER_OBJECT.
 * Solution: call IoCreateDriver() which creates a proper kernel driver object
 * and invokes our DriverMain callback with it, from which we can create our
 * device and symlink normally.
 *
 * IOCTLs (defined in shared.h / Driver.h):
 *   IOCTL_READ_MEMORY   — MmCopyVirtualMemory: game → cheat process
 *   IOCTL_WRITE_MEMORY  — MmCopyVirtualMemory: cheat → game process
 *   IOCTL_GET_MODULE    — PEB ImageBaseAddress of target process
 *
 * Debugging:
 *   - DebugView (Sysinternals) with "Capture Kernel" — filter "BlazeDriver:"
 *   - WinDbg over KD pipe/USB/Net for live kernel debugging
 *
 * Note on unload:
 *   KDMapper-loaded drivers cannot be safely unloaded via sc stop.
 *   A reboot removes the driver from memory.
 *   IOCTL_UNLOAD is provided so the device/symlink can be cleaned up at
 *   runtime (optional, call before rebooting if you care about cleanup).
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "Driver.h"

// ─────────────────────────────────────────────────────────────────────────────
//  ntoskrnl exports not declared in public WDK headers
// ─────────────────────────────────────────────────────────────────────────────

// IoCreateDriver — creates a DRIVER_OBJECT and calls InitializationFunction
// with it. Available since NT 5.1, exported but not in public headers.
NTKERNELAPI
NTSTATUS
IoCreateDriver(
    _In_opt_ PUNICODE_STRING        DriverName,
    _In_     PDRIVER_INITIALIZE     InitializationFunction
);

// MmCopyVirtualMemory — safe cross-process VA copy at PASSIVE_LEVEL
NTKERNELAPI
NTSTATUS
MmCopyVirtualMemory(
    _In_  PEPROCESS  SourceProcess,
    _In_  PVOID      SourceAddress,
    _In_  PEPROCESS  TargetProcess,
    _Out_ PVOID      TargetAddress,
    _In_  SIZE_T     BufferSize,
    _In_  KPROCESSOR_MODE PreviousMode,
    _Out_ PSIZE_T    ReturnSize
);

// ZwQueryInformationProcess — for PEB walking (module base)
typedef enum _PROCESSINFOCLASS_INTERNAL {
    ProcessBasicInformation_Internal = 0
} PROCESSINFOCLASS_INTERNAL;

typedef struct _PROCESS_BASIC_INFORMATION_INTERNAL {
    PVOID     Reserved1;
    PVOID     PebBaseAddress;
    PVOID     Reserved2[2];
    ULONG_PTR UniqueProcessId;
    PVOID     Reserved3;
} PROCESS_BASIC_INFORMATION_INTERNAL;

NTSYSCALLAPI
NTSTATUS
ZwQueryInformationProcess(
    _In_       HANDLE                          ProcessHandle,
    _In_       PROCESSINFOCLASS_INTERNAL       ProcessInformationClass,
    _Out_      PVOID                           ProcessInformation,
    _In_       ULONG                           ProcessInformationLength,
    _Out_opt_  PULONG                          ReturnLength
);

// ─────────────────────────────────────────────────────────────────────────────
//  Globals
// ─────────────────────────────────────────────────────────────────────────────

static PDEVICE_OBJECT g_DeviceObject = NULL;

// ─────────────────────────────────────────────────────────────────────────────
//  KernelCopyMemory
// ─────────────────────────────────────────────────────────────────────────────
NTSTATUS
KernelCopyMemory(
    _In_  PEPROCESS   SourceProcess,
    _In_  PVOID       SourceAddress,
    _In_  PEPROCESS   TargetProcess,
    _Out_ PVOID       TargetAddress,
    _In_  SIZE_T      Size,
    _Out_ PSIZE_T     BytesCopied
)
{
    if (!SourceProcess || !TargetProcess || !SourceAddress || !TargetAddress || Size == 0)
        return STATUS_INVALID_PARAMETER;

    if (KeGetCurrentIrql() > PASSIVE_LEVEL)
        return STATUS_UNSUCCESSFUL;

    // UserMode: MmCopyVirtualMemory probes both VAs and handles page faults via
    // STATUS_ACCESS_VIOLATION instead of BSODing.  Both src (game VA) and dst
    // (cheat process usermode VA) are in user-mode address range so this is correct.
    NTSTATUS st = STATUS_UNSUCCESSFUL;
    __try {
        st = MmCopyVirtualMemory(
            SourceProcess, SourceAddress,
            TargetProcess, TargetAddress,
            Size, UserMode, BytesCopied
        );
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        st = GetExceptionCode();
        *BytesCopied = 0;
    }
    return st;
}

// ─────────────────────────────────────────────────────────────────────────────
//  GetProcessModuleBase — reads PEB.ImageBaseAddress via MmCopyVirtualMemory
// ─────────────────────────────────────────────────────────────────────────────
UINT64
GetProcessModuleBase(
    _In_ ULONG ProcessId
)
{
    PEPROCESS  process  = NULL;
    HANDLE     hProcess = NULL;
    UINT64     base     = 0;

    NTSTATUS status = PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)ProcessId, &process);
    if (!NT_SUCCESS(status)) {
        KdPrint(("BlazeDriver: PsLookupProcessByProcessId(%lu) failed: 0x%X\n", ProcessId, status));
        return 0;
    }

    status = ObOpenObjectByPointer(
        process, OBJ_KERNEL_HANDLE, NULL,
        PROCESS_ALL_ACCESS, *PsProcessType, KernelMode, &hProcess
    );

    if (NT_SUCCESS(status)) {
        PROCESS_BASIC_INFORMATION_INTERNAL pbi = { 0 };
        ULONG returnLength = 0;

        status = ZwQueryInformationProcess(
            hProcess,
            ProcessBasicInformation_Internal,
            &pbi, sizeof(pbi), &returnLength
        );

        if (NT_SUCCESS(status) && pbi.PebBaseAddress) {
            // PEB+0x10 = ImageBaseAddress (x64 Windows 10/11)
            // Destination is a kernel stack variable so KernelMode is required here.
            // Wrap in SEH so an invalid PEB address returns 0 instead of BSODing.
            SIZE_T bytesCopied  = 0;
            PVOID  readSource   = (PVOID)((ULONG_PTR)pbi.PebBaseAddress + 0x10);

            __try {
                MmCopyVirtualMemory(
                    process,               readSource,
                    PsGetCurrentProcess(), (PVOID)&base,
                    sizeof(UINT64), KernelMode, &bytesCopied
                );
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                base = 0;
            }
        }
        ZwClose(hProcess);
    }

    ObDereferenceObject(process);
    return base;
}

// ─────────────────────────────────────────────────────────────────────────────
//  IRP: Create / Close
// ─────────────────────────────────────────────────────────────────────────────
NTSTATUS
IrpCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    KdPrint(("BlazeDriver: IRP_MJ_CREATE\n"));
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS
IrpClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    KdPrint(("BlazeDriver: IRP_MJ_CLOSE\n"));
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
//  IRP: DeviceIoControl
// ─────────────────────────────────────────────────────────────────────────────
NTSTATUS
IrpDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION  stack      = IoGetCurrentIrpStackLocation(Irp);
    ULONG               ioctl      = stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG               inLen      = stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG               outLen     = stack->Parameters.DeviceIoControl.OutputBufferLength;
    PVOID               sysBuffer  = Irp->AssociatedIrp.SystemBuffer;

    NTSTATUS   status = STATUS_SUCCESS;
    ULONG_PTR  info   = 0;

    switch (ioctl)
    {
    // ── READ ─────────────────────────────────────────────────────────────────
    case IOCTL_READ_MEMORY:
    {
        if (inLen < sizeof(MEMORY_REQUEST) || outLen < sizeof(MEMORY_REQUEST)) {
            status = STATUS_BUFFER_TOO_SMALL; break;
        }
        PMEMORY_REQUEST req = (PMEMORY_REQUEST)sysBuffer;
        KdPrint(("BlazeDriver: READ  pid=%lu addr=0x%llX sz=%lu\n",
                 req->processId, req->address, req->size));

        if (req->size == 0 || req->size > 64 * 1024 * 1024)
            { status = STATUS_INVALID_PARAMETER; break; }

        PEPROCESS src = NULL, dst = NULL;
        SIZE_T copied = 0;
        req->response = 0;

        if (!NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)req->processId, &src)))
            { status = STATUS_NOT_FOUND; break; }

        if (!NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)req->cheatId, &dst)))
            { ObDereferenceObject(src); status = STATUS_NOT_FOUND; break; }

        status = KernelCopyMemory(src, (PVOID)req->address,
                                  dst, (PVOID)req->buffer,
                                  (SIZE_T)req->size, &copied);
        req->response = (UINT64)copied;
        info = sizeof(MEMORY_REQUEST);

        ObDereferenceObject(src);
        ObDereferenceObject(dst);
        break;
    }

    // ── WRITE ────────────────────────────────────────────────────────────────
    case IOCTL_WRITE_MEMORY:
    {
        if (inLen < sizeof(MEMORY_REQUEST) || outLen < sizeof(MEMORY_REQUEST)) {
            status = STATUS_BUFFER_TOO_SMALL; break;
        }
        PMEMORY_REQUEST req = (PMEMORY_REQUEST)sysBuffer;
        KdPrint(("BlazeDriver: WRITE pid=%lu addr=0x%llX sz=%lu\n",
                 req->processId, req->address, req->size));

        if (req->size == 0 || req->size > 64 * 1024 * 1024)
            { status = STATUS_INVALID_PARAMETER; break; }

        PEPROCESS src = NULL, dst = NULL;
        SIZE_T copied = 0;
        req->response = 0;

        if (!NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)req->cheatId,    &src)))
            { status = STATUS_NOT_FOUND; break; }

        if (!NT_SUCCESS(PsLookupProcessByProcessId((HANDLE)(ULONG_PTR)req->processId,  &dst)))
            { ObDereferenceObject(src); status = STATUS_NOT_FOUND; break; }

        status = KernelCopyMemory(src, (PVOID)req->buffer,
                                  dst, (PVOID)req->address,
                                  (SIZE_T)req->size, &copied);
        req->response = (UINT64)copied;
        info = sizeof(MEMORY_REQUEST);

        ObDereferenceObject(src);
        ObDereferenceObject(dst);
        break;
    }

    // ── GET MODULE BASE ───────────────────────────────────────────────────────
    case IOCTL_GET_MODULE:
    {
        if (inLen < sizeof(MODULE_REQUEST) || outLen < sizeof(MODULE_REQUEST)) {
            status = STATUS_BUFFER_TOO_SMALL; break;
        }
        PMODULE_REQUEST req = (PMODULE_REQUEST)sysBuffer;
        KdPrint(("BlazeDriver: GET_MODULE pid=%lu\n", req->processId));

        req->baseAddress = GetProcessModuleBase(req->processId);
        info = sizeof(MODULE_REQUEST);
        break;
    }

    // ── UNLOAD (optional cleanup — remove device/symlink while staying mapped) 
    case IOCTL_UNLOAD:
    {
        KdPrint(("BlazeDriver: IOCTL_UNLOAD received — cleaning up device.\n"));

        UNICODE_STRING symLink = RTL_CONSTANT_STRING(SYMLINK_NAME);
        IoDeleteSymbolicLink(&symLink);

        if (g_DeviceObject) {
            IoDeleteDevice(g_DeviceObject);
            g_DeviceObject = NULL;
        }

        // Note: the driver code itself remains mapped (KDMapper). Only the
        // device/symlink is gone. A reboot fully removes the driver from memory.
        info = 0;
        break;
    }

    default:
        KdPrint(("BlazeDriver: Unknown IOCTL 0x%X\n", ioctl));
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status      = status;
    Irp->IoStatus.Information = info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

// ─────────────────────────────────────────────────────────────────────────────
//  DriverUnload — called if the driver is unloaded via normal sc stop.
//  With KDMapper this is never called (no service registration). It's kept
//  here for compatibility if the driver is ever loaded normally.
// ─────────────────────────────────────────────────────────────────────────────
VOID
DriverUnloadRoutine(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNICODE_STRING symLink = RTL_CONSTANT_STRING(SYMLINK_NAME);
    IoDeleteSymbolicLink(&symLink);
    if (g_DeviceObject) IoDeleteDevice(g_DeviceObject);
    KdPrint(("BlazeDriver: Unloaded (normal path).\n"));
}

// ─────────────────────────────────────────────────────────────────────────────
//  DriverMain — called by IoCreateDriver with a real, fully valid DRIVER_OBJECT.
//  This is where we set up device, symlink, and dispatch table.
// ─────────────────────────────────────────────────────────────────────────────
NTSTATUS
DriverMain(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    KdPrint(("BlazeDriver: DriverMain — creating device.\n"));

    UNICODE_STRING devName = RTL_CONSTANT_STRING(DEVICE_NAME);
    UNICODE_STRING symLink = RTL_CONSTANT_STRING(SYMLINK_NAME);

    // Create the device object
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        0,
        &devName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &g_DeviceObject
    );

    if (!NT_SUCCESS(status)) {
        KdPrint(("BlazeDriver: IoCreateDevice failed: 0x%X\n", status));
        return status;
    }

    // Symbolic link \\DosDevices\\BlazeDriver → \\Device\\BlazeDriver
    status = IoCreateSymbolicLink(&symLink, &devName);
    if (!NT_SUCCESS(status)) {
        KdPrint(("BlazeDriver: IoCreateSymbolicLink failed: 0x%X\n", status));
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    // Buffered I/O (METHOD_BUFFERED in our CTL_CODEs)
    g_DeviceObject->Flags |= DO_BUFFERED_IO;
    g_DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    // Dispatch table
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = IrpCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = IrpClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IrpDeviceControl;
    DriverObject->DriverUnload                          = DriverUnloadRoutine;

    KdPrint(("BlazeDriver: Loaded via KDMapper. Device=%wZ Symlink=%wZ\n",
             &devName, &symLink));
    return STATUS_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
//  DriverEntry — entry point called by KDMapper with (NULL, NULL).
//  Delegates to IoCreateDriver which gives us a real DRIVER_OBJECT via DriverMain.
// ─────────────────────────────────────────────────────────────────────────────
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    KdPrint(("BlazeDriver: DriverEntry (KDMapper path).\n"));

    // IoCreateDriver allocates a real DRIVER_OBJECT and calls DriverMain with it.
    // Passing NULL creates an anonymous driver object, preventing name collisions.
    return IoCreateDriver(NULL, &DriverMain);
}

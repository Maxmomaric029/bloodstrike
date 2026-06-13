/*++
Copyright (c) 2024  BloodStrike ESP Kernel Driver

Module Name:
    driver.c

Description:
    Kernel driver that exposes memory read IOCTLs for a usermode ESP overlay.
    Uses MmCopyVirtualMemory (secure) instead of attaching to the process.
--*/

#include <ntddk.h>
#include "common.h"

// -------------------- Global State --------------------
PDEVICE_OBJECT  g_DeviceObject = NULL;
UNICODE_STRING  g_DeviceName;
UNICODE_STRING  g_SymlinkName;

// -------------------- Forward Declarations --------------------
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
VOID     DriverUnload(PDRIVER_OBJECT DriverObject);
NTSTATUS CreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS OnDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);

// -------------------- Internal Helper --------------------

//
// Safe cross-process memory read using MmCopyVirtualMemory.
// Available on Windows 8.1 / Server 2012 R2 and later.
//
NTSTATUS ReadProcessMemoryInternal(HANDLE ProcessId, ULONG64 Address, PVOID Buffer, ULONG64 Size)
{
    NTSTATUS          status;
    PEPROCESS          targetProcess = NULL;
    SIZE_T             bytesRead = 0;

    if (Address == 0 || Buffer == NULL || Size == 0)
        return STATUS_INVALID_PARAMETER;

    status = PsLookupProcessByProcessId(ProcessId, &targetProcess);
    if (!NT_SUCCESS(status))
        return STATUS_NOT_FOUND;

    __try
    {
        // MmCopyVirtualMemory handles cross-process context safely
        status = MmCopyVirtualMemory(
            targetProcess,          // Source process
            (PVOID)(ULONG_PTR)Address, // Source address
            PsGetCurrentProcess(),  // Destination process (our driver)
            Buffer,                 // Destination buffer
            (SIZE_T)Size,           // Number of bytes to read
            KernelMode,             // Access mode
            &bytesRead              // Bytes actually read
        );
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status = GetExceptionCode();
    }

    ObDereferenceObject(targetProcess);
    return status;
}

// -------------------- IOCTL Handlers --------------------

NTSTATUS
HandleReadMemory(
    PIRP Irp,
    PMEMORY_READ_REQUEST Request,
    ULONG InputLength,
    PVOID OutputBuffer,
    ULONG OutputLength,
    PULONG_PTR Information
)
{
    UNREFERENCED_PARAMETER(Irp);
    NTSTATUS status;

    if (InputLength < sizeof(MEMORY_READ_REQUEST))
        return STATUS_BUFFER_TOO_SMALL;

    if (Request->ProcessId == NULL || Request->Address == 0 || Request->Size == 0)
        return STATUS_INVALID_PARAMETER;

    if (Request->Size > MAX_BUFFER_SIZE)
        return STATUS_BUFFER_OVERFLOW;

    if (OutputLength < Request->Size)
        return STATUS_BUFFER_TOO_SMALL;

    status = ReadProcessMemoryInternal(
        Request->ProcessId,
        Request->Address,
        OutputBuffer,
        Request->Size
    );

    if (NT_SUCCESS(status))
        *Information = (ULONG_PTR)Request->Size;
    else
        *Information = 0;

    return status;
}

NTSTATUS
HandleReadChain(
    PIRP Irp,
    PMEMORY_CHAIN_REQUEST Request,
    ULONG InputLength,
    PVOID OutputBuffer,
    ULONG OutputLength,
    PULONG_PTR Information
)
{
    UNREFERENCED_PARAMETER(Irp);
    NTSTATUS status;
    ULONG64  currentAddress;
    ULONG64  nextAddress;
    ULONG    i;

    if (InputLength < sizeof(MEMORY_CHAIN_REQUEST))
        return STATUS_BUFFER_TOO_SMALL;

    if (Request->ProcessId == NULL || Request->OffsetCount == 0 || Request->OffsetCount > MAX_CHAIN_DEPTH)
        return STATUS_INVALID_PARAMETER;

    if (Request->Size > MAX_BUFFER_SIZE)
        return STATUS_BUFFER_OVERFLOW;

    if (OutputLength < Request->Size)
        return STATUS_BUFFER_TOO_SMALL;

    // Follow the pointer chain
    currentAddress = Request->BaseAddress;

    for (i = 0; i < Request->OffsetCount; i++)
    {
        // Read the pointer at currentAddress + Offsets[i]
        status = ReadProcessMemoryInternal(
            Request->ProcessId,
            currentAddress + Request->Offsets[i],
            &nextAddress,
            sizeof(ULONG64)
        );

        if (!NT_SUCCESS(status))
        {
            *Information = 0;
            return status;
        }

        currentAddress = nextAddress;
    }

    // Now currentAddress holds the final pointer value.
    // If the final read is a pointer dereference, we read from currentAddress.
    // Otherwise, currentAddress is already the value we want.
    // Here we read Size bytes from the final resolved address.
    status = ReadProcessMemoryInternal(
        Request->ProcessId,
        currentAddress,
        OutputBuffer,
        Request->Size
    );

    if (NT_SUCCESS(status))
        *Information = (ULONG_PTR)Request->Size;
    else
        *Information = 0;

    return status;
}

// -------------------- Driver Routines --------------------

NTSTATUS
CreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
OnDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION  irpStack;
    NTSTATUS            status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR           information = 0;

    irpStack = IoGetCurrentIrpStackLocation(Irp);

    PVOID  inputBuffer  = Irp->AssociatedIrp.SystemBuffer;
    ULONG  inputLength  = irpStack->Parameters.DeviceIoControl.InputBufferLength;
    PVOID  outputBuffer = Irp->AssociatedIrp.SystemBuffer;  // Same buffer = METHOD_BUFFERED
    ULONG  outputLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;

    switch (irpStack->Parameters.DeviceIoControl.IoControlCode)
    {
    case IOCTL_READ_MEMORY:
        if (inputBuffer != NULL && inputLength >= sizeof(MEMORY_READ_REQUEST))
        {
            status = HandleReadMemory(
                Irp,
                (PMEMORY_READ_REQUEST)inputBuffer,
                inputLength,
                outputBuffer,
                outputLength,
                &information
            );
        }
        break;

    case IOCTL_READ_CHAIN:
        if (inputBuffer != NULL && inputLength >= sizeof(MEMORY_CHAIN_REQUEST))
        {
            status = HandleReadChain(
                Irp,
                (PMEMORY_CHAIN_REQUEST)inputBuffer,
                inputLength,
                outputBuffer,
                outputLength,
                &information
            );
        }
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status      = status;
    Irp->IoStatus.Information = information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return status;
}

// -------------------- Driver Entry / Exit --------------------

NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrint("[BS_Driver] DriverEntry called.\n");

    // Set up unload routine
    DriverObject->DriverUnload = DriverUnload;

    // Set up dispatch routines
    DriverObject->MajorFunction[IRP_MJ_CREATE]         = CreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]          = CreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = OnDeviceControl;

    // Create device name
    RtlInitUnicodeString(&g_DeviceName, DEVICE_NAME);
    RtlInitUnicodeString(&g_SymlinkName, SYMLINK_NAME);

    // Create the device object
    status = IoCreateDevice(
        DriverObject,
        0,                          // Device extension size
        &g_DeviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,    // Secure open
        FALSE,                      // Exclusive
        &g_DeviceObject
    );

    if (!NT_SUCCESS(status))
    {
        DbgPrint("[BS_Driver] IoCreateDevice failed: 0x%X\n", status);
        return status;
    }

    // Create symbolic link so usermode can open \\.\BS_KernelDriver
    status = IoCreateSymbolicLink(&g_SymlinkName, &g_DeviceName);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[BS_Driver] IoCreateSymbolicLink failed: 0x%X\n", status);
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
        return status;
    }

    DbgPrint("[BS_Driver] Driver loaded successfully.\n");
    DbgPrint("[BS_Driver] Device: %wZ  Symlink: %wZ\n", &g_DeviceName, &g_SymlinkName);

    return STATUS_SUCCESS;
}

VOID
DriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    DbgPrint("[BS_Driver] DriverUnload called.\n");

    if (g_DeviceObject)
    {
        IoDeleteSymbolicLink(&g_SymlinkName);
        IoDeleteDevice(g_DeviceObject);
        g_DeviceObject = NULL;
    }

    DbgPrint("[BS_Driver] Driver unloaded successfully.\n");
}

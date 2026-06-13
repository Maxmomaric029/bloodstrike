#ifndef COMMON_H
#define COMMON_H

// ------------------------------------------------------------
// This header is shared between the kernel driver (included
// via ntddk.h) and the usermode app (included via windows.h).
// We include the right header for CTL_CODE() depending on
// context, so that this file is self-contained.
// ------------------------------------------------------------
#ifdef _NTDDK_
    // Already included via ntddk.h in the kernel driver
#elif defined(_WIN32)
    #include <winioctl.h>   // For CTL_CODE in user mode
#endif

#ifdef __cplusplus
extern "C" {
#endif

// -------------------- IOCTL Definitions --------------------
#define IOCTL_READ_MEMORY CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_READ_CHAIN   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define DEVICE_NAME          L"\\Device\\BS_KernelDriver"
#define SYMLINK_NAME         L"\\DosDevices\\BS_KernelDriver"
#define DOS_DEVICE_NAME      L"\\\\.\\BS_KernelDriver"

#define MAX_CHAIN_DEPTH 32
#define MAX_BUFFER_SIZE 4096

// -------------------- Request Structures --------------------

// Simple single-address read request
typedef struct _MEMORY_READ_REQUEST {
    HANDLE  ProcessId;
    ULONG64 Address;
    ULONG64 Size;
    // Followed by: UCHAR Buffer[Size]
} MEMORY_READ_REQUEST, *PMEMORY_READ_REQUEST;

// Chained pointer read request: BaseAddress + Offsets[0..OffsetCount-1]
// Reads *(*(BaseAddress + O0) + O1 ... + O(N-1)) then copies Size bytes
typedef struct _MEMORY_CHAIN_REQUEST {
    HANDLE  ProcessId;
    ULONG64 BaseAddress;
    ULONG   OffsetCount;
    ULONG64 Offsets[MAX_CHAIN_DEPTH];
    ULONG64 Size;
    // Followed by: UCHAR Buffer[Size]
} MEMORY_CHAIN_REQUEST, *PMEMORY_CHAIN_REQUEST;

#ifdef __cplusplus
}
#endif

#endif // COMMON_H

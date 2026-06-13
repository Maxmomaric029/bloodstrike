#pragma once

#include <windows.h>
#include <winternl.h>
#include <vector>
#include <cstdint>
#include <string>
#include <iostream>
#include <cstring>

// NT_SUCCESS may not be defined by all Windows SDK versions via winternl.h alone
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

// ---------------------------------------------------------------------------
// MemoryReader — reads game memory via direct NT API calls
//
//   Bypass techniques:
//   - Resolves NtOpenProcess + NtReadVirtualMemory at runtime from ntdll.dll
//     (no static IAT imports that anticheats can hook)
//   - Opens process handle ONCE and reuses it (avoids repeated OpenProcess)
//   - Uses PROCESS_VM_READ only (minimum required rights)
//   - Separate process overlay (no injection into the game)
// ---------------------------------------------------------------------------

// NT API function pointer types
typedef NTSTATUS (NTAPI* pfnNtOpenProcess)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PCLIENT_ID);
typedef NTSTATUS (NTAPI* pfnNtReadVirtualMemory)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);

class MemoryReader
{
private:
    HANDLE m_hProcess;
    DWORD  m_processId;
    bool   m_connected;

    // Dynamically resolved NT functions
    pfnNtOpenProcess       m_NtOpenProcess;
    pfnNtReadVirtualMemory m_NtReadVirtualMemory;

    bool ResolveFunctions()
    {
        HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
        if (!hNtdll)
        {
            std::cerr << "[MemoryReader] Cannot get ntdll.dll handle.\n";
            return false;
        }

        m_NtOpenProcess       = (pfnNtOpenProcess)GetProcAddress(hNtdll, "NtOpenProcess");
        m_NtReadVirtualMemory = (pfnNtReadVirtualMemory)GetProcAddress(hNtdll, "NtReadVirtualMemory");

        if (!m_NtOpenProcess || !m_NtReadVirtualMemory)
        {
            std::cerr << "[MemoryReader] Failed to resolve NT functions.\n";
            return false;
        }

        return true;
    }

public:
    MemoryReader()
        : m_hProcess(NULL)
        , m_processId(0)
        , m_connected(false)
        , m_NtOpenProcess(nullptr)
        , m_NtReadVirtualMemory(nullptr)
    {
        ResolveFunctions();
    }

    ~MemoryReader()
    {
        Close();
    }

    MemoryReader(const MemoryReader&) = delete;
    MemoryReader& operator=(const MemoryReader&) = delete;

    // -----------------------------------------------------------------------
    // Open — open a handle to the target process via NtOpenProcess
    // -----------------------------------------------------------------------
    bool Open(DWORD processId)
    {
        Close(); // Close any existing handle

        if (!m_NtOpenProcess)
            return false;

        m_processId = processId;

        OBJECT_ATTRIBUTES oa;
        CLIENT_ID         cid;

        InitializeObjectAttributes(&oa, NULL, 0, NULL, NULL);
        cid.UniqueProcess = (HANDLE)(ULONG_PTR)processId;
        cid.UniqueThread  = NULL;

        NTSTATUS status = m_NtOpenProcess(
            &m_hProcess,
            PROCESS_VM_READ,
            &oa,
            &cid
        );

        if (!NT_SUCCESS(status) || !m_hProcess)
        {
            std::cerr << "[MemoryReader] NtOpenProcess failed. PID: " << processId
                      << " Status: 0x" << std::hex << status << std::dec << "\n";
            m_connected = false;
            return false;
        }

        m_connected = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // Close — release the process handle
    // -----------------------------------------------------------------------
    void Close()
    {
        if (m_hProcess)
        {
            CloseHandle(m_hProcess);
            m_hProcess = NULL;
        }
        m_connected = false;
        m_processId = 0;
    }

    // -----------------------------------------------------------------------
    // isConnected — check if process handle is valid
    // -----------------------------------------------------------------------
    bool isConnected() const
    {
        return m_connected && m_hProcess != NULL;
    }

    // -----------------------------------------------------------------------
    // ReadMemory<T> — read a single value of type T from the target process
    // -----------------------------------------------------------------------
    template<typename T>
    bool ReadMemory(uint64_t address, T& out) const
    {
        return ReadMemoryRaw(address, &out, sizeof(T));
    }

    // -----------------------------------------------------------------------
    // ReadMemoryRaw — read raw bytes into a pre-allocated buffer
    // -----------------------------------------------------------------------
    bool ReadMemoryRaw(uint64_t address, void* buffer, size_t size) const
    {
        if (!m_hProcess || !buffer || size == 0 || address == 0)
            return false;

        SIZE_T bytesRead = 0;
        NTSTATUS status = m_NtReadVirtualMemory(
            m_hProcess,
            (PVOID)address,
            buffer,
            size,
            &bytesRead
        );

        return NT_SUCCESS(status) && bytesRead == size;
    }

    // -----------------------------------------------------------------------
    // ReadChain<T> — resolve a chain of offsets and read T at the final address
    //
    //  final_addr = *(*(BaseAddress + O[0]) + O[1] ... + O[N-1])
    //  reads sizeof(T) bytes from final_addr into |out|
    // -----------------------------------------------------------------------
    template<typename T>
    bool ReadChain(uint64_t baseAddress, const std::vector<uint64_t>& offsets, T& out) const
    {
        if (offsets.empty() || !m_hProcess)
            return false;

        uint64_t currentAddress = baseAddress;

        for (size_t i = 0; i < offsets.size(); i++)
        {
            uint64_t nextAddress = 0;
            if (!ReadMemory<uint64_t>(currentAddress + offsets[i], nextAddress))
                return false;

            if (nextAddress == 0)
                return false;

            currentAddress = nextAddress;
        }

        return ReadMemory<T>(currentAddress, out);
    }
};

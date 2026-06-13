#pragma once

#include <windows.h>
#include <vector>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <iostream>

#include "../Kernel/common.h"

// ---------------------------------------------------------------------------
// KM_Driver — communicates with \\.\BS_KernelDriver via DeviceIoControl
// ---------------------------------------------------------------------------
class KM_Driver
{
private:
    HANDLE m_hDriver;

public:
    KM_Driver() : m_hDriver(INVALID_HANDLE_VALUE)
    {
        m_hDriver = CreateFileW(
            DOS_DEVICE_NAME,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (m_hDriver == INVALID_HANDLE_VALUE)
        {
            DWORD err = GetLastError();
            std::cerr << "[KM_Driver] Failed to open driver handle. Error: 0x" << std::hex << err << std::dec << "\n";
            // Don't throw — let caller check isConnected()
        }
    }

    ~KM_Driver()
    {
        if (m_hDriver != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_hDriver);
            m_hDriver = INVALID_HANDLE_VALUE;
        }
    }

    KM_Driver(const KM_Driver&) = delete;
    KM_Driver& operator=(const KM_Driver&) = delete;

    // -----------------------------------------------------------------------
    // ReadMemory<T> — read a single value of type T at |address| in process |pid|
    // -----------------------------------------------------------------------
    template<typename T>
    bool ReadMemory(HANDLE pid, uint64_t address, T& out) const
    {
        return ReadMemoryRaw(pid, address, &out, sizeof(T));
    }

    // -----------------------------------------------------------------------
    // ReadMemoryRaw — read raw bytes into a pre-allocated buffer
    // -----------------------------------------------------------------------
    bool ReadMemoryRaw(HANDLE pid, uint64_t address, void* buffer, size_t size) const
    {
        MEMORY_READ_REQUEST request = { 0 };
        request.ProcessId = pid;
        request.Address   = address;
        request.Size      = size;

        // Allocate a contiguous buffer for the request + output
        std::vector<uint8_t> buf(sizeof(MEMORY_READ_REQUEST) + size);
        memcpy(buf.data(), &request, sizeof(MEMORY_READ_REQUEST));

        DWORD bytesReturned = 0;

        BOOL success = DeviceIoControl(
            m_hDriver,
            IOCTL_READ_MEMORY,
            buf.data(),
            (DWORD)(sizeof(MEMORY_READ_REQUEST) + size),
            buf.data() + sizeof(MEMORY_READ_REQUEST),
            (DWORD)size,
            &bytesReturned,
            NULL
        );

        if (success && bytesReturned == size)
        {
            memcpy(buffer, buf.data() + sizeof(MEMORY_READ_REQUEST), size);
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // ReadChain<T> — resolve a chain of offsets and read T at the final address
    //
    //  final_addr = *(*(BaseAddress + O[0]) + O[1] ... + O[N-1])
    //  reads sizeof(T) bytes from final_addr into |out|
    // -----------------------------------------------------------------------
    template<typename T>
    bool ReadChain(HANDLE pid, uint64_t baseAddress, const std::vector<uint64_t>& offsets, T& out) const
    {
        if (offsets.empty() || offsets.size() > MAX_CHAIN_DEPTH)
            return false;

        MEMORY_CHAIN_REQUEST request = { 0 };
        request.ProcessId   = pid;
        request.BaseAddress = baseAddress;
        request.OffsetCount = (ULONG)offsets.size();
        request.Size        = sizeof(T);

        for (size_t i = 0; i < offsets.size(); i++)
            request.Offsets[i] = offsets[i];

        // Build contiguous buffer
        std::vector<uint8_t> buf(sizeof(MEMORY_CHAIN_REQUEST) + sizeof(T));
        memcpy(buf.data(), &request, sizeof(MEMORY_CHAIN_REQUEST));

        DWORD bytesReturned = 0;

        BOOL success = DeviceIoControl(
            m_hDriver,
            IOCTL_READ_CHAIN,
            buf.data(),
            (DWORD)(sizeof(MEMORY_CHAIN_REQUEST) + sizeof(T)),
            buf.data() + sizeof(MEMORY_CHAIN_REQUEST),
            (DWORD)sizeof(T),
            &bytesReturned,
            NULL
        );

        if (success && bytesReturned == sizeof(T))
        {
            memcpy(&out, buf.data() + sizeof(MEMORY_CHAIN_REQUEST), sizeof(T));
            return true;
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // isConnected — check if driver handle is valid
    // -----------------------------------------------------------------------
    bool isConnected() const
    {
        return m_hDriver != INVALID_HANDLE_VALUE;
    }
};

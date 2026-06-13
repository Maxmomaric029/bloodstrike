#pragma once
#include <cstdint>
#include <cstring>

// ============================================================
// Safe memory reading — all game memory access goes through here.
// Uses SEH (__try/__except) to gracefully handle invalid reads.
// For an injected DLL, pointers are dereferenced directly (no
// ReadProcessMemory needed — we're in the same address space).
// ============================================================

template<typename T>
inline T SafeRead(uintptr_t address) {
    T value{};
    __try {
        value = *(volatile T*)address;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return value;
}

// Read a pointer-sized value
inline uintptr_t ReadPtr(uintptr_t address) {
    return SafeRead<uintptr_t>(address);
}

// Read raw bytes
inline bool ReadRaw(uintptr_t address, void* buffer, size_t size) {
    if (!address || !buffer || size == 0) return false;
    __try {
        memcpy(buffer, (void*)address, size);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Chain of pointer dereferences:
//   base + offsets[0] -> deref -> + offsets[1] -> deref -> ... -> + offsets[N-1] -> deref -> read T
template<typename T>
inline T ReadChain(uintptr_t base, const uintptr_t* offsets, size_t count) {
    uintptr_t current = base;
    for (size_t i = 0; i < count; i++) {
        current = ReadPtr(current + offsets[i]);
        if (current == 0) return T{};
    }
    return SafeRead<T>(current);
}

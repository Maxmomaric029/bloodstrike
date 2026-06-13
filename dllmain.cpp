// ============================================================
// dllmain.cpp — Entry point for BloodStrike internal ESP DLL
//
// Injected into Game.exe. DllMain spawns MainThread which
// initializes DX11 hooks and ImGui overlay.
// ============================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "hooks.h"

// ============================================================
// Main thread — waits for game, then hooks DX11
// ============================================================
static DWORD WINAPI MainThread(LPVOID hModule) {
    // Wait for game module to be ready
    while (!GetModuleHandleA(NULL)) Sleep(100);
    Sleep(2000);  // Let DX11 initialize

    // Extract vtable addresses from a dummy DX11 device
    if (!CreateDummyDevice()) return 1;

    // Install trampoline hooks on Present + ResizeBuffers
    if (!InstallHooks()) return 1;

    // Keep thread alive (hooks persist in process memory)
    while (true) Sleep(1000);
    return 0;
}

// ============================================================
// DllMain — DLL entry point
// ============================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
    }
    return TRUE;
}

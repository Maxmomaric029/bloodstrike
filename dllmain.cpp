// ============================================================
// dllmain.cpp — Entry point for BloodStrike internal ESP DLL
//
// Injected into Game.exe. DllMain spawns MainThread which
// initializes DX11 hooks and ImGui overlay.
// ============================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>

#include "hooks.h"

// ============================================================
// Debug console helper (defined here since hooks.cpp's copy is dead)
// ============================================================
static void OpenConsole() {
    FILE* f;
    AllocConsole();
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
}

// ============================================================
// Main thread — waits for game, then hooks DX11
// ============================================================
static DWORD WINAPI MainThread(LPVOID hModule) {
    OpenConsole();
    printf("[DLL] MainThread started.\n");

    while (!GetModuleHandleA(NULL)) Sleep(100);
    Sleep(3000);

    printf("[DLL] Creating dummy device...\n");
    if (!CreateDummyDevice()) {
        printf("[DLL] Failed to create dummy device.\n");
        return 1;
    }

    printf("[DLL] Installing vtable hooks...\n");
    if (!InstallHooks()) {
        printf("[DLL] Failed to install hooks.\n");
        return 1;
    }

    printf("[DLL] Hooks active. ESP will render on next Present call.\n");
    printf("[DLL] Press Insert to toggle menu.\n");

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

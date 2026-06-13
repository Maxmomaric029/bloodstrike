// ============================================================
// hooks.cpp — DX11 vtable hook with maximum crash protection
// ============================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>

#include "hooks.h"
#include "offsets.h"
#include "memory.h"
#include "esp.h"
#include "menu.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================
// Globals
// ============================================================
PresentFn       oPresent       = nullptr;
ResizeBuffersFn oResizeBuffers = nullptr;
ID3D11Device*           g_Device    = nullptr;
ID3D11DeviceContext*    g_Context   = nullptr;
IDXGISwapChain*         g_SwapChain = nullptr;
ID3D11RenderTargetView* g_RTV       = nullptr;
HWND                    g_Hwnd      = nullptr;
bool                    g_Initialized = false;

static WNDPROC g_OldWndProc = nullptr;
static void**  g_SwapChainVTable = nullptr;  // saved from CreateDummyDevice

// ============================================================
// Safe RTV management
// ============================================================
static void CreateRTV() {
    if (!g_SwapChain || !g_Device) return;
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = g_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (FAILED(hr) || !backBuffer) return;
    if (g_RTV) { g_RTV->Release(); g_RTV = nullptr; }
    g_Device->CreateRenderTargetView(backBuffer, nullptr, &g_RTV);
    backBuffer->Release();
}

// ============================================================
// WndProc hook
// ============================================================
static LRESULT __stdcall HookWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (IsMenuOpen()) {
        if (ImGui_ImplWin32_WndProcHandler(h, m, w, l))
            return 0;
    }
    if (m == WM_KEYDOWN && w == VK_INSERT) {
        ToggleMenu();
        return 0;
    }
    if (g_OldWndProc)
        return CallWindowProcW(g_OldWndProc, h, m, w, l);
    return DefWindowProcW(h, m, w, l);
}

// ============================================================
// Present Hook — heavily guarded against crashes
// ============================================================
HRESULT __stdcall HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (!pSwapChain) {
        if (oPresent) return oPresent(pSwapChain, SyncInterval, Flags);
        return E_FAIL;
    }

    __try {
        if (!g_Initialized) {
            printf("[DLL] HookedPresent: first call, initializing...\n");

            // Get device + context
            g_SwapChain = pSwapChain;
            HRESULT hr = pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_Device);
            if (FAILED(hr) || !g_Device) {
                printf("[DLL] GetDevice failed: 0x%08X\n", hr);
                goto call_original;
            }
            printf("[DLL] Device: %p\n", g_Device);

            g_Device->GetImmediateContext(&g_Context);
            if (!g_Context) {
                printf("[DLL] GetImmediateContext returned null!\n");
                goto call_original;
            }
            printf("[DLL] Context: %p\n", g_Context);

            CreateRTV();
            if (!g_RTV) {
                printf("[DLL] WARNING: CreateRTV failed, ESP may not render\n");
            }

            // Find game window
            DWORD pid = GetCurrentProcessId();
            g_Hwnd = nullptr;
            EnumWindows([](HWND h, LPARAM lp) -> BOOL {
                DWORD wpid;
                GetWindowThreadProcessId(h, &wpid);
                if (wpid == (DWORD)lp && IsWindowVisible(h)) {
                    g_Hwnd = h;
                    return FALSE;
                }
                return TRUE;
            }, (LPARAM)pid);

            if (!g_Hwnd) {
                printf("[DLL] ERROR: Could not find game window!\n");
                goto call_original;
            }
            printf("[DLL] Game window: %p\n", g_Hwnd);

            // Init ImGui
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            ImGui::StyleColorsDark();
            ImGui_ImplWin32_Init(g_Hwnd);
            ImGui_ImplDX11_Init(g_Device, g_Context);
            printf("[DLL] ImGui initialized.\n");

            // Hook WndProc
            g_OldWndProc = (WNDPROC)SetWindowLongPtrW(g_Hwnd, GWLP_WNDPROC, (LONG_PTR)HookWndProc);
            if (!g_OldWndProc) {
                printf("[DLL] WARNING: SetWindowLongPtrW failed (error %lu)\n", GetLastError());
            } else {
                printf("[DLL] WndProc hooked: old=%p\n", g_OldWndProc);
            }

            g_Initialized = true;
            printf("[DLL] ===== READY =====\n");
        }

        // Only render if fully initialized
        if (g_Device && g_Context && g_RTV) {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            RenderESP();
            RenderMenu();

            ImGui::Render();
            g_Context->OMSetRenderTargets(1, &g_RTV, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("[DLL] EXCEPTION in HookedPresent: 0x%08X\n", GetExceptionCode());
    }

call_original:
    if (oPresent)
        return oPresent(pSwapChain, SyncInterval, Flags);
    return E_FAIL;
}

// ============================================================
// ResizeBuffers Hook
// ============================================================
HRESULT __stdcall HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT b, UINT w, UINT h, DXGI_FORMAT f, UINT flags) {
    __try {
        if (g_RTV) { g_RTV->Release(); g_RTV = nullptr; }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    HRESULT hr = E_FAIL;
    if (oResizeBuffers)
        hr = oResizeBuffers(pSwapChain, b, w, h, f, flags);

    __try { CreateRTV(); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return hr;
}

// ============================================================
// Create dummy device + find vtable
// ============================================================
bool CreateDummyDevice() {
    printf("[DLL] Creating dummy DX11 device...\n");

    HINSTANCE hInst = GetModuleHandleA(NULL);
    WNDCLASSEXA wc = { sizeof(wc), CS_CLASSDC, DefWindowProcA, 0, 0,
                       hInst, nullptr, nullptr, nullptr, nullptr,
                       "DX11Dummy", nullptr };
    UnregisterClassA("DX11Dummy", hInst);
    if (!RegisterClassExA(&wc)) {
        printf("[DLL] RegisterClassExA failed: %lu\n", GetLastError());
        return false;
    }

    HWND hDummy = CreateWindowA("DX11Dummy", "", WS_OVERLAPPEDWINDOW,
                                0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hDummy) {
        printf("[DLL] CreateWindowA failed: %lu\n", GetLastError());
        UnregisterClassA("DX11Dummy", wc.hInstance);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount       = 1;
    sd.BufferDesc.Width  = 2;
    sd.BufferDesc.Height = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow      = hDummy;
    sd.SampleDesc.Count  = 1;
    sd.Windowed          = TRUE;
    sd.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &sd, &sc, &dev, &fl, &ctx);

    if (FAILED(hr) || !sc) {
        printf("[DLL] Dummy device failed: 0x%08X\n", hr);
        DestroyWindow(hDummy);
        UnregisterClassA("DX11Dummy", wc.hInstance);
        return false;
    }

    // Read vtable
    void** vtable = *(void***)sc;
    if (!vtable) {
        printf("[DLL] ERROR: VTable is null!\n");
        sc->Release(); dev->Release(); ctx->Release();
        DestroyWindow(hDummy);
        UnregisterClassA("DX11Dummy", wc.hInstance);
        return false;
    }

    g_SwapChainVTable = vtable;  // save vtable pointer (in DLL .rdata, persists after Release)
    oPresent       = (PresentFn)vtable[8];
    oResizeBuffers = (ResizeBuffersFn)vtable[13];

    printf("[DLL] VTable=%p  Present=%p  ResizeBuffers=%p\n", vtable, oPresent, oResizeBuffers);

    if (!oPresent || !oResizeBuffers) {
        printf("[DLL] ERROR: VTable entries are null!\n");
        sc->Release(); dev->Release(); ctx->Release();
        DestroyWindow(hDummy);
        UnregisterClassA("DX11Dummy", wc.hInstance);
        return false;
    }

    sc->Release();
    dev->Release();
    ctx->Release();
    DestroyWindow(hDummy);
    UnregisterClassA("DX11Dummy", wc.hInstance);

    printf("[DLL] Dummy device OK.\n");
    return true;
}

// ============================================================
// Install vtable hooks
// COM vtables are shared across all instances of the same class,
// so the vtable we found from the dummy device IS the same one
// the game's swap chain uses. Just modify it in place.
// ============================================================
bool InstallHooks() {
    if (!g_SwapChainVTable || !oPresent || !oResizeBuffers) {
        printf("[DLL] Cannot install hooks: vtable or originals not set\n");
        return false;
    }

    printf("[DLL] Installing vtable hooks on %p...\n", g_SwapChainVTable);
    DWORD oldProt;

    // Hook Present (index 8)
    if (!VirtualProtect(&g_SwapChainVTable[8], sizeof(void*), PAGE_READWRITE, &oldProt)) {
        printf("[DLL] VirtualProtect failed for Present: %lu\n", GetLastError());
        return false;
    }
    g_SwapChainVTable[8] = (void*)HookedPresent;
    VirtualProtect(&g_SwapChainVTable[8], sizeof(void*), oldProt, &oldProt);
    printf("[DLL] Present hooked: %p -> %p\n", oPresent, HookedPresent);

    // Hook ResizeBuffers (index 13)
    if (!VirtualProtect(&g_SwapChainVTable[13], sizeof(void*), PAGE_READWRITE, &oldProt)) {
        printf("[DLL] VirtualProtect failed for ResizeBuffers: %lu\n", GetLastError());
        return false;
    }
    g_SwapChainVTable[13] = (void*)HookedResizeBuffers;
    VirtualProtect(&g_SwapChainVTable[13], sizeof(void*), oldProt, &oldProt);
    printf("[DLL] ResizeBuffers hooked: %p -> %p\n", oResizeBuffers, HookedResizeBuffers);

    printf("[DLL] VTable hooks installed successfully!\n");
    return true;
}

// ============================================================
// hooks.cpp — DX11 vtable hook with verbose debug logging
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
static void**  g_SwapChainVTable = nullptr;

// ============================================================
// RTV management
// ============================================================
static void CreateRTV() {
    if (!g_SwapChain || !g_Device) return;
    ID3D11Texture2D* backBuffer = nullptr;
    g_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (backBuffer) {
        g_Device->CreateRenderTargetView(backBuffer, nullptr, &g_RTV);
        backBuffer->Release();
    }
}

// ============================================================
// WndProc hook — must forward ALL input to ImGui
// ============================================================
static LRESULT __stdcall HookWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // Always forward input to ImGui when menu is open
    if (IsMenuOpen()) {
        if (ImGui_ImplWin32_WndProcHandler(h, m, w, l))
            return 0;  // ImGui consumed this input
    }

    // Insert key toggles menu
    if (m == WM_KEYDOWN && w == VK_INSERT) {
        ToggleMenu();
        return 0;
    }

    // Forward everything else to original WndProc
    if (g_OldWndProc)
        return CallWindowProcW(g_OldWndProc, h, m, w, l);
    return DefWindowProcW(h, m, w, l);
}

// ============================================================
// Present Hook
// ============================================================
HRESULT __stdcall HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    __try {
        if (!g_Initialized) {
            g_SwapChain = pSwapChain;
            pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_Device);
            g_Device->GetImmediateContext(&g_Context);
            CreateRTV();

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
                return oPresent(pSwapChain, SyncInterval, Flags);
            }
            printf("[DLL] Game window: 0x%p\n", g_Hwnd);

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
                printf("[DLL] WndProc hooked: old=0x%p\n", g_OldWndProc);
            }

            g_Initialized = true;
            printf("[DLL] ===== READY =====\n");
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render ESP
        RenderESP();

        // Render menu
        RenderMenu();

        ImGui::Render();
        g_Context->OMSetRenderTargets(1, &g_RTV, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("[DLL] EXCEPTION in HookedPresent: 0x%08X\n", GetExceptionCode());
    }

    return oPresent(pSwapChain, SyncInterval, Flags);
}

// ============================================================
// ResizeBuffers Hook
// ============================================================
HRESULT __stdcall HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT b, UINT w, UINT h, DXGI_FORMAT f, UINT flags) {
    __try {
        if (g_RTV) { g_RTV->Release(); g_RTV = nullptr; }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    HRESULT hr = oResizeBuffers(pSwapChain, b, w, h, f, flags);
    __try { CreateRTV(); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return hr;
}

// ============================================================
// Create dummy device + find vtable
// ============================================================
bool CreateDummyDevice() {
    HINSTANCE hInst = GetModuleHandleA(NULL);
    WNDCLASSEXA wc = { sizeof(wc), CS_CLASSDC, DefWindowProcA, 0, 0,
                       hInst, nullptr, nullptr, nullptr, nullptr,
                       "DX11Dummy", nullptr };
    UnregisterClassA("DX11Dummy", hInst);
    RegisterClassExA(&wc);
    HWND hDummy = CreateWindowA("DX11Dummy", "", WS_OVERLAPPEDWINDOW,
                                0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

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

    if (FAILED(hr)) {
        printf("[DLL] Dummy device failed: 0x%08X\n", hr);
        DestroyWindow(hDummy);
        UnregisterClassA("DX11Dummy", wc.hInstance);
        return false;
    }

    g_SwapChainVTable = *(void***)sc;
    oPresent       = (PresentFn)g_SwapChainVTable[8];
    oResizeBuffers = (ResizeBuffersFn)g_SwapChainVTable[13];

    printf("[DLL] VTable=%p  Present=%p  ResizeBuffers=%p\n",
           g_SwapChainVTable, oPresent, oResizeBuffers);

    sc->Release();
    dev->Release();
    ctx->Release();
    DestroyWindow(hDummy);
    UnregisterClassA("DX11Dummy", wc.hInstance);
    return true;
}

// ============================================================
// Install vtable hooks
// ============================================================
bool InstallHooks() {
    if (!g_SwapChainVTable || !oPresent || !oResizeBuffers) return false;
    DWORD oldProt;

    VirtualProtect(&g_SwapChainVTable[8], sizeof(void*), PAGE_READWRITE, &oldProt);
    g_SwapChainVTable[8] = (void*)HookedPresent;
    VirtualProtect(&g_SwapChainVTable[8], sizeof(void*), oldProt, &oldProt);

    VirtualProtect(&g_SwapChainVTable[13], sizeof(void*), PAGE_READWRITE, &oldProt);
    g_SwapChainVTable[13] = (void*)HookedResizeBuffers;
    VirtualProtect(&g_SwapChainVTable[13], sizeof(void*), oldProt, &oldProt);

    printf("[DLL] VTable hooks installed.\n");
    return true;
}

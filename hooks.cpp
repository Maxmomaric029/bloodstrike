// ============================================================
// hooks.cpp — DX11 Present / ResizeBuffers hook implementation
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

// ImGui
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

// WndProc original
static WNDPROC g_OldWndProc = nullptr;

// ============================================================
// Helpers
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
// DX11 Dummy Device — extract vtable addresses
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
        DestroyWindow(hDummy);
        UnregisterClassA("DX11Dummy", wc.hInstance);
        return false;
    }

    void** vtable = *(void***)sc;
    oPresent       = (PresentFn)vtable[8];
    oResizeBuffers = (ResizeBuffersFn)vtable[13];

    sc->Release();
    dev->Release();
    ctx->Release();
    DestroyWindow(hDummy);
    UnregisterClassA("DX11Dummy", wc.hInstance);
    return true;
}

// ============================================================
// Trampoline hook helper (x64 absolute jump)
// ============================================================
static bool HookFunction(void* target, void* hook, void** trampolineOut) {
    void* trampoline = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampoline) return false;

    // Copy first 14 bytes of original
    memcpy(trampoline, target, 14);

    // Append: jmp [rip+0] -> original + 14
    uint8_t* p = (uint8_t*)trampoline + 14;
    p[0] = 0xFF; p[1] = 0x25;
    *(uint32_t*)(p + 2) = 0;
    *(uint64_t*)(p + 6) = (uint64_t)((uint8_t*)target + 14);

    // Patch original to jmp hook
    DWORD oldProt;
    VirtualProtect(target, 14, PAGE_EXECUTE_READWRITE, &oldProt);
    p = (uint8_t*)target;
    p[0] = 0xFF; p[1] = 0x25;
    *(uint32_t*)(p + 2) = 0;
    *(uint64_t*)(p + 6) = (uint64_t)hook;
    VirtualProtect(target, 14, oldProt, &oldProt);

    if (trampolineOut) *trampolineOut = trampoline;
    return true;
}

// ============================================================
// Install hooks on Present + ResizeBuffers
// ============================================================
static void* g_PresentTrampoline = nullptr;
static void* g_ResizeTrampoline  = nullptr;

bool InstallHooks() {
    if (!oPresent || !oResizeBuffers) return false;
    if (!HookFunction((void*)oPresent, (void*)HookedPresent, &g_PresentTrampoline))
        return false;
    if (!HookFunction((void*)oResizeBuffers, (void*)HookedResizeBuffers, &g_ResizeTrampoline))
        return false;
    return true;
}

// ============================================================
// ResizeBuffers Hook
// ============================================================
HRESULT __stdcall HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT b, UINT w, UINT h, DXGI_FORMAT f, UINT flags) {
    if (g_RTV) { g_RTV->Release(); g_RTV = nullptr; }

    // Call original through trampoline
    using Fn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    Fn orig = (Fn)g_ResizeTrampoline;
    HRESULT hr = orig(pSwapChain, b, w, h, f, flags);

    CreateRTV();
    return hr;
}

// ============================================================
// Present Hook
// ============================================================
HRESULT __stdcall HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (!g_Initialized) {
        g_SwapChain = pSwapChain;
        pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_Device);
        g_Device->GetImmediateContext(&g_Context);
        CreateRTV();

        // Find game window
        DWORD pid = GetCurrentProcessId();
        struct Ctx { DWORD pid; HWND out; } ctx = { pid, nullptr };
        EnumWindows([](HWND h, LPARAM lp) -> BOOL {
            auto& c = *(Ctx*)lp;
            DWORD wpid; GetWindowThreadProcessId(h, &wpid);
            if (wpid == c.pid && IsWindowVisible(h)) { c.out = h; return FALSE; }
            return TRUE;
        }, (LPARAM)&ctx);
        g_Hwnd = ctx.out;

        // Init ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();

        ImGui_ImplWin32_Init(g_Hwnd);
        ImGui_ImplDX11_Init(g_Device, g_Context);

        // Hook WndProc for input
        g_OldWndProc = (WNDPROC)SetWindowLongPtrW(g_Hwnd, GWLP_WNDPROC,
            (LONG_PTR)[](HWND h, UINT m, WPARAM w, LPARAM l) -> LRESULT {
                if (IsMenuOpen() && ImGui_ImplWin32_WndProcHandler(h, m, w, l))
                    return 1;
                if (m == WM_KEYDOWN && w == VK_INSERT)
                    ToggleMenu();
                return CallWindowProcW(g_OldWndProc, h, m, w, l);
            });

        g_Initialized = true;
    }

    // Begin ImGui frame
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Render ESP (reads engine chain, entity list, bones)
    if (IsESPEnabled()) {
        RenderESP();
    }

    // Render ImGui menu
    RenderMenu();

    // Finish ImGui
    ImGui::Render();
    g_Context->OMSetRenderTargets(1, &g_RTV, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // Call original Present through trampoline
    using Fn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    Fn orig = (Fn)g_PresentTrampoline;
    return orig(pSwapChain, SyncInterval, Flags);
}

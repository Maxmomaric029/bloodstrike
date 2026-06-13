#pragma once
// ============================================================
// hooks.h — DX11 Present / ResizeBuffers hook
// ============================================================

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdint.h>

typedef HRESULT(__stdcall* PresentFn)(IDXGISwapChain*, UINT, UINT);
typedef HRESULT(__stdcall* ResizeBuffersFn)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

// Original function pointers (set by CreateDummyDevice)
extern PresentFn       oPresent;
extern ResizeBuffersFn oResizeBuffers;

// DX11 device / context / swap chain (set by HookedPresent on first call)
extern ID3D11Device*           g_Device;
extern ID3D11DeviceContext*    g_Context;
extern IDXGISwapChain*         g_SwapChain;
extern ID3D11RenderTargetView* g_RTV;
extern HWND                    g_Hwnd;
extern bool                    g_Initialized;

// Create a dummy DX11 device to extract vtable addresses
bool CreateDummyDevice();

// Install trampoline hooks on Present + ResizeBuffers
bool InstallHooks();

// Hooked functions (called via trampoline)
HRESULT __stdcall HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
HRESULT __stdcall HookedResizeBuffers(IDXGISwapChain* pSwapChain, UINT b, UINT w, UINT h, DXGI_FORMAT f, UINT flags);

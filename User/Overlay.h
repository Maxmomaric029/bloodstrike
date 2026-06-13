#pragma once

// ============================================================================
// Overlay.h
//
// DirectX 11 overlay window (click-through, topmost, transparent).
// Provides drawing primitives for ESP: boxes, lines, skeleton, text.
// ============================================================================

#include <windows.h>
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <string>
#include <vector>
#include <cmath>

#include "SDK_BloodStrike.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

// ---------------------------------------------------------------------------
// ESP feature toggles (shared state between Overlay and main loop)
// ---------------------------------------------------------------------------
struct ESPToggles
{
    bool box       = true;   // Corner box around players
    bool skeleton  = true;   // Bone skeleton lines
    bool healthBar = true;   // Vertical health bar
    bool infoText  = true;   // HP / distance text
    bool visible   = true;   // Global toggle — Insert key toggles this
};

// ---------------------------------------------------------------------------
// ESP Drawing primitives
// ---------------------------------------------------------------------------
struct Line2D
{
    Vector2 start, end;
    Color   color;
    float   thickness;
};

struct Box2D
{
    Vector2 topLeft, bottomRight;
    Color   color;
    float   thickness;
    bool    filled;
    Color   fillColor;
};

struct Text2D
{
    Vector2 position;
    std::wstring text;
    Color   color;
    float   size;
};

// ---------------------------------------------------------------------------
// Overlay class
// ---------------------------------------------------------------------------
class Overlay
{
private:
    // Window
    HINSTANCE   m_hInstance;
    HWND        m_hWnd;
    HWND        m_targetWnd;
    int         m_screenWidth;
    int         m_screenHeight;
    bool        m_running;

    // D3D11
    ComPtr<ID3D11Device>            m_d3dDevice;
    ComPtr<ID3D11DeviceContext>     m_d3dContext;
    ComPtr<IDXGISwapChain>          m_swapChain;
    ComPtr<ID3D11RenderTargetView>  m_renderTargetView;

    // D2D1
    ComPtr<ID2D1Factory>            m_d2dFactory;
    ComPtr<ID2D1RenderTarget>       m_d2dRenderTarget;
    ComPtr<ID2D1SolidColorBrush>    m_d2dBrush;

    // DirectWrite
    ComPtr<IDWriteFactory>          m_dwriteFactory;
    ComPtr<IDWriteTextFormat>       m_dwriteTextFormat;

    // ESP toggles (shared state)
    ESPToggles m_toggles;

    // Draw list (filled each frame)
    std::vector<Line2D> m_lines;
    std::vector<Box2D>  m_boxes;
    std::vector<Text2D> m_texts;

public:
    Overlay()
        : m_hInstance(GetModuleHandle(NULL))
        , m_hWnd(NULL)
        , m_targetWnd(NULL)
        , m_screenWidth(1920)
        , m_screenHeight(1080)
        , m_running(false)
    {
    }

    // -----------------------------------------------------------------------
    // Access to toggles
    // -----------------------------------------------------------------------
    ESPToggles&       Toggles()       { return m_toggles; }
    const ESPToggles& Toggles() const { return m_toggles; }

    bool IsVisible() const { return m_toggles.visible; }

    ~Overlay()
    {
        Cleanup();
    }

    // -----------------------------------------------------------------------
    // Initialize overlay window and DirectX
    // -----------------------------------------------------------------------
    bool Initialize(HWND targetWindow)
    {
        m_targetWnd = targetWindow;

        // Get target window dimensions
        RECT clientRect;
        if (!GetClientRect(m_targetWnd, &clientRect))
            return false;

        m_screenWidth  = clientRect.right - clientRect.left;
        m_screenHeight = clientRect.bottom - clientRect.top;

        if (m_screenWidth <= 0 || m_screenHeight <= 0)
            return false;

        // Register window class
        const wchar_t CLASS_NAME[] = L"BloodStrikeESP_Overlay";

        WNDCLASSEXW wc = { 0 };
        wc.cbSize        = sizeof(WNDCLASSEXW);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = WndProc;
        wc.hInstance     = m_hInstance;
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)CreateSolidBrush(RGB(0, 0, 0));
        wc.lpszClassName = CLASS_NAME;

        RegisterClassExW(&wc);

        // Create overlay window
        // WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE
        // WS_EX_CLICKTHROUGH is implicit via WS_EX_TRANSPARENT
        m_hWnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            CLASS_NAME,
            L"BloodStrike ESP",
            WS_POPUP,
            clientRect.left, clientRect.top,
            m_screenWidth, m_screenHeight,
            NULL, NULL, m_hInstance, this
        );

        if (!m_hWnd)
            return false;

        // Make the window transparent (alpha blending)
        SetLayeredWindowAttributes(m_hWnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

        // Set window region to be transparent for clicks
        // WS_EX_TRANSPARENT + layered window with color key handles this

        // Initialize DirectX
        if (!InitD3D11())
            return false;

        if (!InitD2D1())
            return false;

        if (!InitDWrite())
            return false;

        // Store this pointer for the window proc
        SetWindowLongPtrW(m_hWnd, GWLP_USERDATA, (LONG_PTR)this);

        ShowWindow(m_hWnd, SW_SHOW);
        UpdateWindow(m_hWnd);

        m_running = true;
        return true;
    }

    // -----------------------------------------------------------------------
    // Main render loop (call once per frame)
    // -----------------------------------------------------------------------
    void BeginFrame()
    {
        // Clear draw lists
        m_lines.clear();
        m_boxes.clear();
        m_texts.clear();

        // Ensure overlay follows target window
        UpdateOverlayPosition();

        // Begin D2D rendering
        if (m_d2dRenderTarget)
        {
            m_d2dRenderTarget->BeginDraw();
            m_d2dRenderTarget->Clear(D2D1::ColorF(0, 0, 0, 0)); // Transparent
        }
    }

    void EndFrame()
    {
        if (m_d2dRenderTarget)
        {
            m_d2dRenderTarget->EndDraw();
        }

        // Present the swap chain
        if (m_swapChain)
        {
            m_swapChain->Present(0, 0);
        }
    }

    void RenderFrame()
    {
        BeginFrame();

        // Draw all primitives
        for (const auto& box : m_boxes)
            DrawBoxInternal(box);

        for (const auto& line : m_lines)
            DrawLineInternal(line);

        for (const auto& text : m_texts)
            DrawTextInternal(text);

        EndFrame();
    }

    // -----------------------------------------------------------------------
    // Drawing API (feed these before RenderFrame)
    // -----------------------------------------------------------------------
    void AddLine(const Vector2& start, const Vector2& end, const Color& color, float thickness = 1.5f)
    {
        m_lines.push_back({ start, end, color, thickness });
    }

    void AddBox(const Vector2& topLeft, const Vector2& bottomRight, const Color& color,
                float thickness = 2.0f, bool filled = false, const Color& fillColor = Color(0, 0, 0, 0))
    {
        m_boxes.push_back({ topLeft, bottomRight, color, thickness, filled, fillColor });
    }

    void AddText(const Vector2& position, const std::wstring& text, const Color& color, float size = 14.0f)
    {
        m_texts.push_back({ position, text, color, size });
    }

    // -----------------------------------------------------------------------
    // Helper: cornered box (2D bounding box with corner marks)
    // -----------------------------------------------------------------------
    void AddCornerBox(const Vector2& topLeft, const Vector2& bottomRight, const Color& color,
                      float thickness = 2.0f, float cornerRatio = 0.2f)
    {
        float width  = bottomRight.x - topLeft.x;
        float height = bottomRight.y - topLeft.y;

        float cornerLenW = width  * cornerRatio;
        float cornerLenH = height * cornerRatio;

        // Top-left corner
        AddLine(topLeft, Vector2(topLeft.x + cornerLenW, topLeft.y), color, thickness);
        AddLine(topLeft, Vector2(topLeft.x, topLeft.y + cornerLenH), color, thickness);

        // Top-right corner
        AddLine(Vector2(bottomRight.x, topLeft.y),
                Vector2(bottomRight.x - cornerLenW, topLeft.y), color, thickness);
        AddLine(Vector2(bottomRight.x, topLeft.y),
                Vector2(bottomRight.x, topLeft.y + cornerLenH), color, thickness);

        // Bottom-left corner
        AddLine(Vector2(topLeft.x, bottomRight.y),
                Vector2(topLeft.x + cornerLenW, bottomRight.y), color, thickness);
        AddLine(Vector2(topLeft.x, bottomRight.y),
                Vector2(topLeft.x, bottomRight.y - cornerLenH), color, thickness);

        // Bottom-right corner
        AddLine(bottomRight,
                Vector2(bottomRight.x - cornerLenW, bottomRight.y), color, thickness);
        AddLine(bottomRight,
                Vector2(bottomRight.x, bottomRight.y - cornerLenH), color, thickness);
    }

    // -----------------------------------------------------------------------
    // Helper: skeleton drawing
    // -----------------------------------------------------------------------
    void AddSkeletonLine(const Vector2& from, const Vector2& to, const Color& color, float thickness = 1.5f)
    {
        // Only draw if points are valid and on screen
        if (from.x > 0 && from.y > 0 && to.x > 0 && to.y > 0)
        {
            AddLine(from, to, color, thickness);
        }
    }

    // -----------------------------------------------------------------------
    // Getters
    // -----------------------------------------------------------------------
    bool     IsRunning()       const { return m_running; }
    HWND     GetWindow()       const { return m_hWnd; }
    int      GetWidth()        const { return m_screenWidth; }
    int      GetHeight()       const { return m_screenHeight; }

    // Process window messages (call in loop)
    void ProcessMessages()
    {
        MSG msg = { 0 };
        while (PeekMessageW(&msg, m_hWnd, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // -----------------------------------------------------------------------
    // Set text format size
    // -----------------------------------------------------------------------
    void SetTextSize(float size)
    {
        if (m_dwriteFactory)
        {
            m_dwriteTextFormat.Reset();
            m_dwriteFactory->CreateTextFormat(
                L"Consolas",
                NULL,
                DWRITE_FONT_WEIGHT_BOLD,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                size,
                L"en-US",
                &m_dwriteTextFormat
            );
        }
    }

private:
    // -----------------------------------------------------------------------
    // Window procedure (static)
    // -----------------------------------------------------------------------
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        Overlay* pThis = (Overlay*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);

        switch (msg)
        {
        case WM_DESTROY:
            if (pThis)
                pThis->m_running = false;
            PostQuitMessage(0);
            return 0;

        case WM_ERASEBKGND:
            return 1; // Prevent flicker

        case WM_NCHITTEST:
            return HTTRANSPARENT; // Pass all mouse input through the overlay

        default:
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // Update overlay position to follow target window
    // -----------------------------------------------------------------------
    void UpdateOverlayPosition()
    {
        if (!m_targetWnd || !m_hWnd)
            return;

        RECT targetRect;
        if (!GetClientRect(m_targetWnd, &targetRect))
            return;

        POINT topLeft = { targetRect.left, targetRect.top };
        ClientToScreen(m_targetWnd, &topLeft);

        int newWidth  = targetRect.right - targetRect.left;
        int newHeight = targetRect.bottom - targetRect.top;

        if (newWidth != m_screenWidth || newHeight != m_screenHeight)
        {
            m_screenWidth  = newWidth;
            m_screenHeight = newHeight;
            ResizeSwapChain();
        }

        SetWindowPos(m_hWnd, HWND_TOPMOST,
                     topLeft.x, topLeft.y,
                     m_screenWidth, m_screenHeight,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }

    // -----------------------------------------------------------------------
    // Initialize Direct3D 11
    // -----------------------------------------------------------------------
    bool InitD3D11()
    {
        DXGI_SWAP_CHAIN_DESC scd = { 0 };
        scd.BufferCount       = 1;
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow      = m_hWnd;
        scd.SampleDesc.Count  = 1;
        scd.Windowed          = TRUE;
        scd.BufferDesc.Width  = m_screenWidth;
        scd.BufferDesc.Height = m_screenHeight;

        D3D_FEATURE_LEVEL featureLevel;
        HRESULT hr = D3D11CreateDeviceAndSwapChain(
            NULL,
            D3D_DRIVER_TYPE_HARDWARE,
            NULL,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, // Required for D2D interop
            NULL, 0,
            D3D11_SDK_VERSION,
            &scd,
            &m_swapChain,
            &m_d3dDevice,
            &featureLevel,
            &m_d3dContext
        );

        if (FAILED(hr))
            return false;

        // Create render target view
        ComPtr<ID3D11Texture2D> backBuffer;
        hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (FAILED(hr))
            return false;

        hr = m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), NULL, &m_renderTargetView);
        if (FAILED(hr))
            return false;

        return true;
    }

    // -----------------------------------------------------------------------
    // Initialize Direct2D 1
    // -----------------------------------------------------------------------
    bool InitD2D1()
    {
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&m_d2dFactory));
        if (FAILED(hr))
            return false;

        // Get DXGI surface from swap chain
        ComPtr<IDXGISurface> dxgiSurface;
        hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiSurface));
        if (FAILED(hr))
            return false;

        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );

        hr = m_d2dFactory->CreateDxgiSurfaceRenderTarget(
            dxgiSurface.Get(), props, &m_d2dRenderTarget
        );
        if (FAILED(hr))
            return false;

        hr = m_d2dRenderTarget->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::White), &m_d2dBrush
        );
        if (FAILED(hr))
            return false;

        return true;
    }

    // -----------------------------------------------------------------------
    // Initialize DirectWrite
    // -----------------------------------------------------------------------
    bool InitDWrite()
    {
        HRESULT hr = DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf())
        );
        if (FAILED(hr))
            return false;

        hr = m_dwriteFactory->CreateTextFormat(
            L"Consolas",
            NULL,
            DWRITE_FONT_WEIGHT_BOLD,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            14.0f,
            L"en-US",
            &m_dwriteTextFormat
        );
        if (FAILED(hr))
            return false;

        m_dwriteTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        m_dwriteTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

        return true;
    }

    // -----------------------------------------------------------------------
    // Resize swap chain when window size changes
    // -----------------------------------------------------------------------
    void ResizeSwapChain()
    {
        if (!m_swapChain || !m_d3dDevice)
            return;

        m_renderTargetView.Reset();
        m_d2dRenderTarget.Reset();

        m_swapChain->ResizeBuffers(0, m_screenWidth, m_screenHeight, DXGI_FORMAT_UNKNOWN, 0);

        ComPtr<ID3D11Texture2D> backBuffer;
        m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        m_d3dDevice->CreateRenderTargetView(backBuffer.Get(), NULL, &m_renderTargetView);

        // Recreate D2D render target
        ComPtr<IDXGISurface> dxgiSurface;
        m_swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiSurface));

        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
        );

        m_d2dFactory->CreateDxgiSurfaceRenderTarget(dxgiSurface.Get(), props, &m_d2dRenderTarget);
        m_d2dRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_d2dBrush);
    }

    // -----------------------------------------------------------------------
    // Internal drawing helpers
    // -----------------------------------------------------------------------
    void DrawLineInternal(const Line2D& line)
    {
        if (!m_d2dRenderTarget || !m_d2dBrush)
            return;

        m_d2dBrush->SetColor(D2D1::ColorF(
            line.color.r / 255.0f,
            line.color.g / 255.0f,
            line.color.b / 255.0f,
            line.color.a / 255.0f
        ));

        m_d2dRenderTarget->DrawLine(
            D2D1::Point2F(line.start.x, line.start.y),
            D2D1::Point2F(line.end.x,   line.end.y),
            m_d2dBrush.Get(),
            line.thickness
        );
    }

    void DrawBoxInternal(const Box2D& box)
    {
        if (!m_d2dRenderTarget || !m_d2dBrush)
            return;

        D2D1_RECT_F rect = D2D1::RectF(
            box.topLeft.x, box.topLeft.y,
            box.bottomRight.x, box.bottomRight.y
        );

        if (box.filled)
        {
            m_d2dBrush->SetColor(D2D1::ColorF(
                box.fillColor.r / 255.0f,
                box.fillColor.g / 255.0f,
                box.fillColor.b / 255.0f,
                box.fillColor.a / 255.0f
            ));
            m_d2dRenderTarget->FillRectangle(rect, m_d2dBrush.Get());
        }

        m_d2dBrush->SetColor(D2D1::ColorF(
            box.color.r / 255.0f,
            box.color.g / 255.0f,
            box.color.b / 255.0f,
            box.color.a / 255.0f
        ));

        m_d2dRenderTarget->DrawRectangle(rect, m_d2dBrush.Get(), box.thickness);
    }

    void DrawTextInternal(const Text2D& text)
    {
        if (!m_d2dRenderTarget || !m_d2dBrush || !m_dwriteTextFormat)
            return;

        m_d2dBrush->SetColor(D2D1::ColorF(
            text.color.r / 255.0f,
            text.color.g / 255.0f,
            text.color.b / 255.0f,
            text.color.a / 255.0f
        ));

        D2D1_RECT_F rect = D2D1::RectF(
            text.position.x, text.position.y,
            text.position.x + 500.0f,  // Wide enough for most text
            text.position.y + 100.0f
        );

        m_d2dRenderTarget->DrawTextW(
            text.text.c_str(),
            (UINT32)text.text.length(),
            m_dwriteTextFormat.Get(),
            &rect,
            m_d2dBrush.Get(),
            D2D1_DRAW_TEXT_OPTIONS_NONE,
            DWRITE_MEASURING_MODE_NATURAL
        );
    }

    // -----------------------------------------------------------------------
    // Cleanup
    // -----------------------------------------------------------------------
    void Cleanup()
    {
        m_d2dBrush.Reset();
        m_d2dRenderTarget.Reset();
        m_d2dFactory.Reset();
        m_dwriteTextFormat.Reset();
        m_dwriteFactory.Reset();
        m_renderTargetView.Reset();
        m_d3dContext.Reset();
        m_swapChain.Reset();
        m_d3dDevice.Reset();

        if (m_hWnd)
        {
            DestroyWindow(m_hWnd);
            m_hWnd = NULL;
        }
    }
};

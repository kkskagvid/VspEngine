#include "RuntimePCH.h"

#include <Windowsx.h>

#include "Engine/Core/Logging/Log.h"
#include "Engine/Core/Platform.h"
#include "Engine/Core/Window.h"
#include "Engine/Core/String/StringConv.h"
#include "Engine/Events/WindowEvents.h"
#include "Engine/Events/InputEvents.h"

#include "Platforms/Windows/WindowsWindow.h"

namespace Vsp
{
    static constexpr const char* kLogTag = "WindowsWindow";

    WindowProperties WindowsWindow::GetProperties() const
    {
        return m_Properties;
    }

    WindowsWindow::WindowsWindow(const WindowProperties& properties)
    {
        if (!Create(properties))
        {
            LOG_ERROR(kLogTag, "Failed to create window.");
        }
    }

    bool WindowsWindow::Create(const WindowProperties& properties)
    {
        m_Properties = properties;

        m_HInstance = ::GetModuleHandleW(nullptr);

        std::string className = properties.Title + " Window Class";
        m_ClassName = ConvertToWString(className.c_str());

        std::wstring wTitle = ConvertToWString(properties.Title.c_str());

        WNDCLASSEXW windowClass = {};
        windowClass.cbSize        = sizeof(WNDCLASSEXW);
        windowClass.style         = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc   = WindowProc;
        windowClass.cbClsExtra    = 0;
        windowClass.cbWndExtra    = 0;
        windowClass.hInstance     = m_HInstance;
        windowClass.hIcon         = LoadIconW(m_HInstance, MAKEINTRESOURCEW(105));
        windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        windowClass.lpszMenuName  = nullptr;
        windowClass.lpszClassName = m_ClassName.c_str();
        windowClass.hIconSm       = LoadIconW(m_HInstance, MAKEINTRESOURCEW(101));

        if (!RegisterClassExW(&windowClass))
        {
            LOG_ERROR(kLogTag, "RegisterClassExW failed.");
            return false;
        }

        DWORD windowStyle   = 0;
        DWORD windowExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;

        RECT windowRect  = {};
        int  windowX     = CW_USEDEFAULT;
        int  windowY     = CW_USEDEFAULT;

        switch (m_Properties.DisplayMode)
        {
        case WindowProperties::WindowDisplayMode::Windowed:
        {
            windowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
            windowRect.right  = static_cast<LONG>(m_Properties.Width);
            windowRect.bottom = static_cast<LONG>(m_Properties.Height);
            AdjustWindowRectEx(&windowRect, windowStyle, FALSE, windowExStyle);

            // Center on the primary monitor's work area (excludes taskbar)
            HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi = {};
            mi.cbSize = sizeof(MONITORINFO);
            if (GetMonitorInfoW(monitor, &mi))
            {
                const RECT& work = mi.rcWork;
                LONG winW = windowRect.right  - windowRect.left;
                LONG winH = windowRect.bottom - windowRect.top;
                windowX = work.left + (work.right  - work.left - winW) / 2;
                windowY = work.top  + (work.bottom - work.top  - winH) / 2;
                if (windowX < work.left) windowX = work.left;
                if (windowY < work.top)  windowY = work.top;
            }
            break;
        }

        case WindowProperties::WindowDisplayMode::WindowedBorderless:
        {
            windowStyle = WS_POPUP | WS_VISIBLE;

            // Size to the primary monitor
            HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO mi = {};
            mi.cbSize = sizeof(MONITORINFO);
            if (GetMonitorInfoW(monitor, &mi))
            {
                windowRect = mi.rcMonitor;
                windowX    = mi.rcMonitor.left;
                windowY    = mi.rcMonitor.top;
            }
            break;
        }

        case WindowProperties::WindowDisplayMode::FullScreen:
        {
            windowStyle = WS_POPUP | WS_VISIBLE;

            // Try to switch to the requested display mode first
            uint32_t fsW = m_Properties.Width;
            uint32_t fsH = m_Properties.Height;
            if (ChangeToFullscreenDisplayMode(fsW, fsH, 0))
            {
                windowRect.right  = static_cast<LONG>(fsW);
                windowRect.bottom = static_cast<LONG>(fsH);
            }
            else
            {
                // Fallback: use desktop resolution as a borderless window
                LOG_WARNING(kLogTag, "Failed to set exclusive fullscreen display mode; falling back to borderless.");
                HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
                MONITORINFO mi = {};
                mi.cbSize = sizeof(MONITORINFO);
                if (GetMonitorInfoW(monitor, &mi))
                {
                    windowRect = mi.rcMonitor;
                    windowX    = mi.rcMonitor.left;
                    windowY    = mi.rcMonitor.top;
                }
                RestoreDisplaySettings();
                m_Properties.DisplayMode = WindowProperties::WindowDisplayMode::WindowedBorderless;
            }
            break;
        }
        }

        m_HWnd = CreateWindowExW(
            windowExStyle,
            m_ClassName.c_str(),
            wTitle.c_str(),
            windowStyle,
            windowX,
            windowY,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr,
            nullptr,
            m_HInstance,
            this);

        if (!m_HWnd)
        {
            LOG_ERROR(kLogTag, "CreateWindowExW failed.");
            return false;
        }

        m_HDC = GetDC(m_HWnd);

        if (m_Properties.DisplayMode == WindowProperties::WindowDisplayMode::Windowed)
        {
            SaveWindowedState();
        }
        else
        {
            // When starting in borderless/fullscreen, seed the windowed
            // state with sensible defaults so we can return to windowed later.
            m_WindowedState.Style   = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
            m_WindowedState.ExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
            m_WindowedState.Width   = static_cast<int>(properties.Width);
            m_WindowedState.Height  = static_cast<int>(properties.Height);
            m_WindowedState.X       = CW_USEDEFAULT;
            m_WindowedState.Y       = CW_USEDEFAULT;
        }

        ShowWindow(m_HWnd, SW_SHOW);
        UpdateWindow(m_HWnd);

        return true;
    }

    void WindowsWindow::Destroy()
    {
        // Restore display settings before destroying (important for
        // exclusive fullscreen — prevents leaving the desktop at a
        // non-native resolution after the process exits).
        RestoreDisplaySettings();

        if (m_HDC)
        {
            ReleaseDC(m_HWnd, m_HDC);
            m_HDC = nullptr;
        }
        if (m_HWnd)
        {
            DestroyWindow(m_HWnd);
            m_HWnd = nullptr;
        }

        UnregisterClassW(m_ClassName.c_str(), m_HInstance);
    }

    void WindowsWindow::Update()
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    void WindowsWindow::GetClientSize(int& outWidth, int& outHeight) const
    {
        if (m_HWnd)
        {
            RECT rect;
            GetClientRect(m_HWnd, &rect);
            outWidth  = rect.right - rect.left;
            outHeight = rect.bottom - rect.top;
        }
        else
        {
            outWidth = outHeight = 0;
        }
    }

    void WindowsWindow::SetDisplayMode(
        WindowProperties::WindowDisplayMode mode,
        uint32_t fullscreenWidth,
        uint32_t fullscreenHeight,
        uint32_t refreshRate)
    {
        if (!m_HWnd)
            return;

        // No-op if already in the requested mode
        if (m_Properties.DisplayMode == mode)
            return;

        // Store the requested fullscreen configuration
        m_FullscreenConfig.Width       = fullscreenWidth;
        m_FullscreenConfig.Height      = fullscreenHeight;
        m_FullscreenConfig.RefreshRate = refreshRate;

        // If the window is minimized, restore it first so the transition
        // rectangle captures the correct screen coordinates.
        if (IsIconic(m_HWnd))
            ShowWindow(m_HWnd, SW_RESTORE);

        switch (mode)
        {
        case WindowProperties::WindowDisplayMode::Windowed:
            EnterWindowed();
            break;

        case WindowProperties::WindowDisplayMode::WindowedBorderless:
            EnterBorderless();
            break;

        case WindowProperties::WindowDisplayMode::FullScreen:
            EnterExclusiveFullscreen();
            break;
        }
    }

    void WindowsWindow::EnterWindowed()
    {
        // ── 1.  Restore the desktop display mode (if we changed it) ─────
        RestoreDisplaySettings();

        // ── 2.  Ensure we have a valid saved state ─────────────────────
        // The saved state always reflects the last-known windowed
        // configuration. If it was never saved (edge case), use defaults.
        if (m_WindowedState.Width <= 0 || m_WindowedState.Height <= 0)
        {
            m_WindowedState.Style   = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
            m_WindowedState.ExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
            m_WindowedState.X       = static_cast<int>(CW_USEDEFAULT);
            m_WindowedState.Y       = static_cast<int>(CW_USEDEFAULT);
            m_WindowedState.Width   = static_cast<int>(m_Properties.Width);
            m_WindowedState.Height  = static_cast<int>(m_Properties.Height);
        }

        m_IsTransitioning = true;

        // ── 3.  Restore the windowed style, extended-style, and rect ──
        DWORD style   = m_WindowedState.Style;
        DWORD exStyle = m_WindowedState.ExStyle;

        SetWindowLongPtrW(m_HWnd, GWL_STYLE,   static_cast<LONG_PTR>(style));
        SetWindowLongPtrW(m_HWnd, GWL_EXSTYLE, static_cast<LONG_PTR>(exStyle));

        // ── 4.  The saved rect is already a window rect (from
        // rcNormalPosition); use its dimensions directly without
        // re-running AdjustWindowRectEx.
        LONG x      = m_WindowedState.X;
        LONG y      = m_WindowedState.Y;
        LONG width  = m_WindowedState.Width;
        LONG height = m_WindowedState.Height;

        // Clamp to a minimum sensible size
        if (width  < 200) width  = 200;
        if (height < 150) height = 150;

        UINT flags = SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE;

        // CW_USEDEFAULT is only meaningful for CreateWindowEx, not
        // SetWindowPos. If the saved position was CW_USEDEFAULT (e.g. the
        // window started in fullscreen), pick a sensible cascade position.
        if (x == CW_USEDEFAULT || y == CW_USEDEFAULT)
        {
            x = 100;
            y = 100;
        }

        SetWindowPos(m_HWnd, HWND_NOTOPMOST, x, y, width, height, flags);

        // ── 5.  If the window was previously maximized, maximize it ────
        if (m_WindowedState.WasMaximized)
            ShowWindow(m_HWnd, SW_MAXIMIZE);

        m_IsTransitioning = false;
        m_Properties.DisplayMode = WindowProperties::WindowDisplayMode::Windowed;
    }

    void WindowsWindow::EnterBorderless()
    {
        // ── 1.  Restore display mode if leaving exclusive fullscreen ────
        RestoreDisplaySettings();

        // ── 2.  Save current state if coming from windowed mode ─────────
        if (m_Properties.DisplayMode == WindowProperties::WindowDisplayMode::Windowed)
        {
            SaveWindowedState();
        }

        // ── 3.  Determine target monitor and its full bounds ────────────
        HMONITOR monitor = GetTargetMonitor();
        MONITORINFO mi = {};
        mi.cbSize = sizeof(MONITORINFO);
        if (!GetMonitorInfoW(monitor, &mi))
        {
            LOG_ERROR(kLogTag, "GetMonitorInfoW failed in EnterBorderless.");
            return;
        }

        const RECT& area = mi.rcMonitor;
        int x      = area.left;
        int y      = area.top;
        int width  = area.right  - area.left;
        int height = area.bottom - area.top;

        m_IsTransitioning = true;
        ApplyWindowStyleAndPos(
            WS_POPUP | WS_VISIBLE,
            WS_EX_APPWINDOW,
            x, y, width, height);
        m_IsTransitioning = false;

        m_Properties.DisplayMode = WindowProperties::WindowDisplayMode::WindowedBorderless;
    }

    void WindowsWindow::EnterExclusiveFullscreen()
    {
        // ── 1.  Only save state when coming directly from windowed mode.
        // If we are already in borderless the original windowed state
        // was saved by EnterBorderless and must not be overwritten.
        if (m_Properties.DisplayMode == WindowProperties::WindowDisplayMode::Windowed)
        {
            SaveWindowedState();
        }

        // ── 2.  Determine target resolution ────────────────────────────
        uint32_t targetWidth  = m_FullscreenConfig.Width;
        uint32_t targetHeight = m_FullscreenConfig.Height;
        uint32_t targetRate   = m_FullscreenConfig.RefreshRate;

        // If no explicit resolution was provided, query the monitor's
        // current (desktop) resolution so we match it 1:1.
        if (targetWidth == 0 || targetHeight == 0)
        {
            HMONITOR monitor = GetTargetMonitor();
            MONITORINFOEXW mi = {};
            mi.cbSize = sizeof(MONITORINFOEXW);
            if (GetMonitorInfoW(monitor, &mi))
            {
                DEVMODEW dm = {};
                dm.dmSize = sizeof(DEVMODEW);
                if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
                {
                    targetWidth  = dm.dmPelsWidth;
                    targetHeight = dm.dmPelsHeight;
                    if (targetRate == 0)
                        targetRate = dm.dmDisplayFrequency;
                }
                else
                {
                    targetWidth  = static_cast<uint32_t>(mi.rcMonitor.right  - mi.rcMonitor.left);
                    targetHeight = static_cast<uint32_t>(mi.rcMonitor.bottom - mi.rcMonitor.top);
                }
            }
        }

        m_FullscreenConfig.Width       = targetWidth;
        m_FullscreenConfig.Height      = targetHeight;
        m_FullscreenConfig.RefreshRate = targetRate;

        // ── 3.  Attempt to change the display mode ──────────────────────
        if (!ChangeToFullscreenDisplayMode(targetWidth, targetHeight, targetRate))
        {
            LOG_ERROR(kLogTag, "ChangeToFullscreenDisplayMode failed; falling back to borderless.");
            EnterBorderless();
            return;
        }

        // ── 4.  Apply borderless, topmost window at the target resolution
        m_IsTransitioning = true;

        HMONITOR monitor = GetTargetMonitor();
        MONITORINFO mi = {};
        mi.cbSize = sizeof(MONITORINFO);
        GetMonitorInfoW(monitor, &mi);

        ApplyWindowStyleAndPos(
            WS_POPUP | WS_VISIBLE,
            WS_EX_APPWINDOW | WS_EX_TOPMOST,
            mi.rcMonitor.left,
            mi.rcMonitor.top,
            static_cast<int>(targetWidth),
            static_cast<int>(targetHeight));

        m_IsTransitioning = false;

        m_Properties.DisplayMode = WindowProperties::WindowDisplayMode::FullScreen;
    }

    void WindowsWindow::SaveWindowedState()
    {
        m_WindowedState.Style   = static_cast<DWORD>(GetWindowLongPtrW(m_HWnd, GWL_STYLE));
        m_WindowedState.ExStyle = static_cast<DWORD>(GetWindowLongPtrW(m_HWnd, GWL_EXSTYLE));

        // Use WINDOWPLACEMENT to record the normal (restored) position and
        // whether the window is currently maximized.
        WINDOWPLACEMENT wp = {};
        wp.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(m_HWnd, &wp);
        m_WindowedState.WasMaximized = (wp.showCmd == SW_SHOWMAXIMIZED);

        // rcNormalPosition is the window rect in workspace coordinates for
        // the normal (restored) state — exactly what we need for restoring.
        m_WindowedState.Rect   = wp.rcNormalPosition;
        m_WindowedState.X      = wp.rcNormalPosition.left;
        m_WindowedState.Y      = wp.rcNormalPosition.top;
        m_WindowedState.Width  = wp.rcNormalPosition.right  - wp.rcNormalPosition.left;
        m_WindowedState.Height = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
    }

    HMONITOR WindowsWindow::GetTargetMonitor() const
    {
        if (m_HWnd)
            return MonitorFromWindow(m_HWnd, MONITOR_DEFAULTTONEAREST);
        return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    }

    bool WindowsWindow::ChangeToFullscreenDisplayMode(
        uint32_t width, uint32_t height, uint32_t refreshRate)
    {
        HMONITOR monitor = GetTargetMonitor();

        MONITORINFOEXW mi = {};
        mi.cbSize = sizeof(MONITORINFOEXW);
        if (!GetMonitorInfoW(monitor, &mi))
        {
            LOG_ERROR(kLogTag, "GetMonitorInfoW failed in ChangeToFullscreenDisplayMode.");
            return false;
        }

        DEVMODEW dm = {};
        dm.dmSize = sizeof(DEVMODEW);

        // Query the current mode first so we inherit any fields we don't
        // explicitly override (colour depth, etc.)
        if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        {
            LOG_ERROR(kLogTag, "EnumDisplaySettingsW failed for the target display device.");
            return false;
        }

        dm.dmPelsWidth  = width;
        dm.dmPelsHeight = height;
        dm.dmFields     = DM_PELSWIDTH | DM_PELSHEIGHT;

        if (refreshRate > 0)
        {
            dm.dmDisplayFrequency = refreshRate;
            dm.dmFields |= DM_DISPLAYFREQUENCY;
        }

        LONG result = ChangeDisplaySettingsExW(
            mi.szDevice,
            &dm,
            nullptr,
            CDS_FULLSCREEN,
            nullptr);

        if (result != DISP_CHANGE_SUCCESSFUL)
        {
            LOG_ERROR(kLogTag, "ChangeDisplaySettingsExW failed with result: {}", static_cast<int>(result));
            return false;
        }

        m_DisplaySettingsChanged = true;
        return true;
    }

    void WindowsWindow::RestoreDisplaySettings()
    {
        if (m_DisplaySettingsChanged)
        {
            ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
            m_DisplaySettingsChanged = false;
        }
    }

    void WindowsWindow::ApplyWindowStyleAndPos(
        DWORD style, DWORD exStyle,
        int x, int y, int width, int height,
        UINT extraFlags)
    {
        SetWindowLongPtrW(m_HWnd, GWL_STYLE,   static_cast<LONG_PTR>(style));
        SetWindowLongPtrW(m_HWnd, GWL_EXSTYLE, static_cast<LONG_PTR>(exStyle));

        UINT flags = SWP_FRAMECHANGED | SWP_NOZORDER | extraFlags;
        SetWindowPos(m_HWnd, nullptr, x, y, width, height, flags);
    }

    LRESULT CALLBACK WindowsWindow::WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        case WM_CREATE:
        {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            WindowsWindow* pThis = reinterpret_cast<WindowsWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
            break;
        }

        case WM_NCDESTROY:
        {
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
            break;
        }

        default:
        {
            WindowsWindow* pThis = reinterpret_cast<WindowsWindow*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
            if (pThis)
            {
                return pThis->HandleMessage(hWnd, uMsg, wParam, lParam);
            }
        }
        }

        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }

    LRESULT WindowsWindow::HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg)
        {
        // ── Window Lifecycle ────────────────────────────────────────────

        case WM_CREATE:
            break;

        case WM_DESTROY:
        case WM_QUIT:
        {
            if (m_EventCallback)
            {
                WindowCloseEvent e;
                m_EventCallback(e);
            }
            ::CloseWindow(hWnd);
            break;
        }

        case WM_CLOSE:
            PostQuitMessage(0);
            break;

        case WM_SIZE:
        {
            if (m_IsTransitioning)
                break;   // Suppress spurious resize events while changing modes

            RECT clientRect;
            GetClientRect(hWnd, &clientRect);
            int w = clientRect.right  - clientRect.left;
            int h = clientRect.bottom - clientRect.top;

            // Track window state
            switch (wParam)
            {
            case SIZE_MINIMIZED:
                m_Properties.State = WindowProperties::WindowState::Minimized;
                break;
            case SIZE_MAXIMIZED:
                m_Properties.State = WindowProperties::WindowState::Maximized;
                break;
            case SIZE_RESTORED:
                m_Properties.State = WindowProperties::WindowState::Normal;
                break;
            }

            if (m_EventCallback)
            {
                WindowResizeEvent e(w, h);
                m_EventCallback(e);
            }
            break;
        }

        // ── Activation ──────────────────────────────────────────────────

        case WM_ACTIVATE:
        {
            // When an exclusive-fullscreen window loses focus, minimize it
            // so the user can interact with other applications.
            if (m_Properties.DisplayMode == WindowProperties::WindowDisplayMode::FullScreen)
            {
                if (LOWORD(wParam) == WA_INACTIVE)
                {
                    // Minimize rather than keeping a fullscreen window
                    // that obscures the desktop while defocused.
                    ShowWindow(hWnd, SW_MINIMIZE);
                }
            }
            break;
        }

        case WM_ACTIVATEAPP:
            break;

        // ── Display Change ──────────────────────────────────────────────

        case WM_DISPLAYCHANGE:
        {
            // The display resolution changed externally. If we are in
            // exclusive fullscreen we may need to react, but for now we
            // simply forward the information via the resize event.
            int newWidth  = static_cast<int>(LOWORD(lParam));
            int newHeight = static_cast<int>(HIWORD(lParam));

            if (m_EventCallback && !m_IsTransitioning)
            {
                WindowResizeEvent e(newWidth, newHeight);
                m_EventCallback(e);
            }
            break;
        }

        case WM_PAINT:
        {
            //PAINTSTRUCT ps;
            //BeginPaint(hWnd, &ps);
            //EndPaint(hWnd, &ps);
            break;
        }

        case WM_LBUTTONDOWN:
            if (m_EventCallback) { MouseButtonPressedEvent e(0); m_EventCallback(e); }
            break;

        case WM_RBUTTONDOWN:
            if (m_EventCallback) { MouseButtonPressedEvent e(1); m_EventCallback(e); }
            break;

        case WM_MBUTTONDOWN:
            if (m_EventCallback) { MouseButtonPressedEvent e(2); m_EventCallback(e); }
            break;

        case WM_LBUTTONUP:
            if (m_EventCallback) { MouseButtonReleasedEvent e(0); m_EventCallback(e); }
            break;

        case WM_RBUTTONUP:
            if (m_EventCallback) { MouseButtonReleasedEvent e(1); m_EventCallback(e); }
            break;

        case WM_MBUTTONUP:
            if (m_EventCallback) { MouseButtonReleasedEvent e(2); m_EventCallback(e); }
            break;

        case WM_MOUSEMOVE:
            if (m_EventCallback)
            {
                MouseMovedEvent e(static_cast<float>(GET_X_LPARAM(lParam)),
                                  static_cast<float>(GET_Y_LPARAM(lParam)));
                m_EventCallback(e);
            }
            break;

        case WM_MOUSEWHEEL:
            if (m_EventCallback)
            {
                float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA);
                MouseScrolledEvent e(0.0f, delta);
                m_EventCallback(e);
            }
            break;

        case WM_KEYDOWN:
            if (m_EventCallback)
            {
                int repeatCount = static_cast<int>(lParam & 0xFFFF);
                KeyPressedEvent e(static_cast<int>(wParam), repeatCount);
                m_EventCallback(e);
            }
            break;

        case WM_KEYUP:
            if (m_EventCallback)
            {
                KeyReleasedEvent e(static_cast<int>(wParam));
                m_EventCallback(e);
            }
            break;

        // ── System Commands ─────────────────────────────────────────────

        case WM_SYSKEYDOWN:
        {
            // Block Alt+Enter (DXGI has been configured to not handle it,
            // but also catch it here to prevent the system default beep).
            if (wParam == VK_RETURN && (lParam & (1 << 29)))
                break;

            // Fall through to default handler for other system keys
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
        }

        default:
            break;
        }

        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

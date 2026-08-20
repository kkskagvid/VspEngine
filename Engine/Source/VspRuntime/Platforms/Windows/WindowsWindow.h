#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Core/Window.h"

namespace Vsp
{
    class WindowsWindow : public Window
    {
    public:
        WindowsWindow(const WindowProperties& properties);
        ~WindowsWindow() override = default;

        void Destroy() override;

        void* GetNativeWindowHandle() const override { return m_HWnd; }
        void* GetNativeDisplayHandle() const override { return m_HDC; }
        void GetClientSize(int& outWidth, int& outHeight) const override;
        void SetEventCallback(EventCallback callback) override { m_EventCallback = std::move(callback); }

        void Update() override;
        WindowProperties GetProperties() const override;

        // ═══════════════════════════════════════════════════════════════
        //  Display Mode Switching
        // ═══════════════════════════════════════════════════════════════

        /**
         * @brief Dynamically switch the window display mode at runtime.
         *
         * Supports three transitions:
         *  - Windowed → Borderless:   window fills the monitor without borders
         *  - Windowed → FullScreen:    exclusive fullscreen (alters display mode)
         *  - Borderless ↔ FullScreen:  transitions between the two fullscreen styles
         *
         * @param mode             Target display mode.
         * @param fullscreenWidth  Width for exclusive fullscreen (0 = use desktop width).
         * @param fullscreenHeight Height for exclusive fullscreen (0 = use desktop height).
         * @param refreshRate      Refresh rate in Hz for exclusive fullscreen (0 = use desktop rate).
         */
        void SetDisplayMode(
            WindowProperties::WindowDisplayMode mode,
            uint32_t fullscreenWidth  = 0,
            uint32_t fullscreenHeight = 0,
            uint32_t refreshRate      = 0) override;

        /** @brief Return the current display mode. */
        WindowProperties::WindowDisplayMode GetDisplayMode() const override { return m_Properties.DisplayMode; }

    private:
        // ── Window object handles ──────────────────────────────────────
        HINSTANCE     m_HInstance = nullptr;
        HWND          m_HWnd      = nullptr;
        HDC           m_HDC       = nullptr;
        VspString     m_ClassName;               ///< Cached window class name for unregistration

        EventCallback    m_EventCallback;
        WindowProperties m_Properties;

        // ── Saved windowed state (restored when leaving fullscreen) ────
        struct WindowedState
        {
            RECT  Rect     = {};
            DWORD Style    = 0;
            DWORD ExStyle  = 0;
            int   X        = 0;
            int   Y        = 0;
            int   Width    = 0;
            int   Height   = 0;
            bool  WasMaximized = false;
        };
        WindowedState m_WindowedState;

        // ── Exclusive fullscreen configuration ──────────────────────────
        struct FullscreenConfig
        {
            uint32_t Width       = 0;
            uint32_t Height      = 0;
            uint32_t RefreshRate = 0;
        };
        FullscreenConfig m_FullscreenConfig;

        // ── State guards ────────────────────────────────────────────────
        bool m_DisplaySettingsChanged = false;   ///< True when ChangeDisplaySettings was used
        bool m_IsTransitioning        = false;   ///< Suppresses erroneous WM_SIZE during mode transitions

        // ═══════════════════════════════════════════════════════════════
        //  Internal Helpers
        // ═══════════════════════════════════════════════════════════════

        bool Create(const WindowProperties& properties);

        /** @brief Transition to standard windowed mode. */
        void EnterWindowed();

        /** @brief Transition to borderless fullscreen-window mode. */
        void EnterBorderless();

        /** @brief Transition to exclusive fullscreen with display mode change. */
        void EnterExclusiveFullscreen();

        /** @brief Save the current window position / style into m_WindowedState. */
        void SaveWindowedState();

        /** @brief Return the monitor the window currently resides on. */
        HMONITOR GetTargetMonitor() const;

        /**
         * @brief Change the display mode for exclusive fullscreen.
         * @return true on success.
         */
        bool ChangeToFullscreenDisplayMode(uint32_t width, uint32_t height, uint32_t refreshRate);

        /** @brief Restore the desktop display mode if we changed it. */
        void RestoreDisplaySettings();

        /** @brief Set window style, extended style, and position in one atomic call. */
        void ApplyWindowStyleAndPos(
            DWORD  style,
            DWORD  exStyle,
            int    x,
            int    y,
            int    width,
            int    height,
            UINT   extraFlags = 0);

        // ═══════════════════════════════════════════════════════════════
        //  Message Handling
        // ═══════════════════════════════════════════════════════════════

        static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    };
}

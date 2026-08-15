#pragma once

#include <string>

#include "Engine/Core/Core.h"
#include "Engine/Events/Event.h"

namespace Vsp
{
    /**
     * @brief 窗口的属性
     */
    struct WindowProperties
    {
        /**
         * @brief 窗口标题。
         */
        std::string Title;

        /**
         * @brief 窗口宽度。
         */
        uint32_t Width;

        /**
         * @brief 窗口宽度。
         */
        uint32_t Height;

        /**
         * @brief 窗口状态。
         */
        enum class WindowState : uint32_t
        {
            Normal,
            Minimized,
            Maximized,
            FullScreen
        };
        WindowState State;

        enum class WindowDisplayMode : uint32_t
        {
            Windowed,
            WindowedBorderless,
            FullScreen,
        };
        WindowDisplayMode DisplayMode;

        WindowProperties(
            std::string title = "Vesper Engine",
            uint32_t width = 1280,
            uint32_t height = 720,
            WindowState state = WindowState::Normal,
            WindowDisplayMode mode = WindowDisplayMode::Windowed
        ) : Title(title), Width(width), Height(height), State(state), DisplayMode(mode)
        {
        }
    };

    class Window
    {
    public:
        virtual ~Window() = default;

        /**
         * @brief 销毁窗口
         */
        virtual void Destroy() = 0;

        virtual WindowProperties GetProperties() const = 0;
        virtual void* GetNativeWindowHandle() const = 0;
        virtual void* GetNativeDisplayHandle() const = 0;
        virtual void GetClientSize(int& outWidth, int& outHeight) const = 0;

        virtual void SetEventCallback(EventCallback callback) = 0;

        virtual void Update() = 0;

        /**
         * @brief Dynamically switch the window display mode at runtime.
         *
         * Transitions the window between Windowed, WindowedBorderless, and
         * FullScreen (exclusive) modes without recreating the window.
         *
         * @param mode             Target display mode.
         * @param fullscreenWidth  Width for exclusive fullscreen (0 = current desktop).
         * @param fullscreenHeight Height for exclusive fullscreen (0 = current desktop).
         * @param refreshRate      Refresh rate in Hz for exclusive fullscreen (0 = current desktop).
         */
        virtual void SetDisplayMode(
            WindowProperties::WindowDisplayMode mode,
            uint32_t fullscreenWidth  = 0,
            uint32_t fullscreenHeight = 0,
            uint32_t refreshRate      = 0) = 0;

        /** @brief Return the current display mode. */
        virtual WindowProperties::WindowDisplayMode GetDisplayMode() const = 0;

        static std::unique_ptr<Window> Create(const WindowProperties& properties = WindowProperties());
    };
}

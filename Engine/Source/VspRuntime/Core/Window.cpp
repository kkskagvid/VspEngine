#include "RuntimePCH.h"

#include <memory>

#include "Core/Core.h"
#include "Core/Window.h"

#if VSP_PLATFORM_WINDOWS
    #include "Platforms/Windows/WindowsWindow.h"
#endif

namespace Vsp
{
    std::unique_ptr<Window> Window::Create(const WindowProperties& properties)
    {
#if VSP_PLATFORM_WINDOWS
        auto window = std::make_unique<WindowsWindow>(properties);
        return window;
#else
#error "Unsupported platform"
        return nullptr;
#endif
    }
}

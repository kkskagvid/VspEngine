#pragma once

#include <memory>

#include "Core/Core.h"
#include "Core/Window.h"
#include "Core/Templates/ArrayList.h"
#include "Core/String/VspString.h"
#include "Events/WindowEvents.h"
#include "Events/EventDispatcher.h"

namespace Vsp
{
	struct ApplicationArguments
	{
		ArrayList<VspString> ArgsName;
		ArrayList<VspString> Args;
	};

	// Owns the window, pumps its messages and routes the resulting events:
	// input events are forwarded into the InputManager, window events are
	// dispatched to the registered listeners.
#pragma warning(push)
#pragma warning(disable : 4251)   // Member classes without dll-interface: window + dispatcher.
	class RUNTIME_API Application
	{
	public:
		Application(const ApplicationArguments& args);
		Application(const ApplicationArguments& args, const WindowProperties& windowProperties);
		bool IsRunning() { return m_IsRunning; }

		void Update();
		void OnEvent(Event& e);

		Window* GetWindow() const { return m_Window.get(); }

	private:
		void OnWindowResize(WindowResizeEvent& event);
		void OnWindowClose(WindowCloseEvent& event);

	private:
		bool m_IsRunning = true;
		EventDispatcher m_EventDispatcher;
		std::unique_ptr<Window> m_Window;
	};
#pragma warning(pop)
}

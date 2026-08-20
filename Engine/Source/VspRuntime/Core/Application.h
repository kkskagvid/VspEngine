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

	class RUNTIME_API Application
	{
	public:
		Application(const ApplicationArguments& args);
		bool IsRunning() { return m_IsRunning; }

		void Update();
		void OnEvent(Event& e);

	private:
		void OnWindowResize(WindowResizeEvent& event);
		void OnWindowClose(WindowCloseEvent& event);

	private:
		bool m_IsRunning = true;
		EventDispatcher m_EventDispatcher;
		std::unique_ptr<Window> m_Window;
	};
}

#pragma once

#include <memory>

#include "Engine/Core/Core.h"
#include "Engine/Core/Window.h"
#include "Engine/Core/Templates/ArrayList.h"
#include "Engine/Core/String/VspString.h"
#include "Engine/Events/WindowEvents.h"
#include "Engine/Events/EventDispatcher.h"

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

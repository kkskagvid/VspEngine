#include "RuntimePCH.h"

#include "Application.h"

namespace Vsp
{
	Application::Application(const ApplicationArguments& args)
	{
		m_Window = Window::Create();
		if (!m_Window)
		{
			//LOG_ERROR(kLogTag, "Failed to create window!");
			return;
		}

		m_Window->SetEventCallback(std::bind(&Application::OnEvent, this, std::placeholders::_1));
		m_EventDispatcher.AddListener<WindowResizeEvent>(std::bind(&Application::OnWindowResize, this, std::placeholders::_1));
		m_EventDispatcher.AddListener<WindowCloseEvent>(std::bind(&Application::OnWindowClose, this, std::placeholders::_1));
	}

	void Application::OnEvent(Event& e)
	{
		m_EventDispatcher.Dispatch(e);
	}

	void Application::OnWindowResize(WindowResizeEvent& event)
	{
		
	}

	void Application::OnWindowClose(WindowCloseEvent & event)
	{
		m_IsRunning = false;
	}
}

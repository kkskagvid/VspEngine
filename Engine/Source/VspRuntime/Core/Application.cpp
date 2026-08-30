#include "RuntimePCH.h"

#include <functional>
#include <memory>

#include "Application.h"
#include "Core/Input/InputManager.h"
#include "Templates/Delegate.h"

namespace Vsp
{
	Application::Application(const ApplicationArguments& args)
		: Application(args, WindowProperties())
	{
	}

	Application::Application(const ApplicationArguments& args, const WindowProperties& windowProperties)
	{
		m_Window = Window::Create(windowProperties);
		if (!m_Window)
		{
			return;
		}

		m_Window->SetEventCallback(std::bind(&Application::OnEvent, this, std::placeholders::_1));
		m_EventDispatcher.AddListener<WindowResizeEvent>(std::bind(&Application::OnWindowResize, this, std::placeholders::_1));
		m_EventDispatcher.AddListener<WindowCloseEvent>(std::bind(&Application::OnWindowClose, this, std::placeholders::_1));
	}

	void Application::Update()
	{
		if (m_Window)
		{
			m_Window->Update();
		}
	}

	void Application::OnEvent(Event& e)
	{
		m_EventDispatcher.Dispatch(e);

		// Forward input events into the engine-wide input state.
		const EventCategory eCategory = e.GetCategory();
		if ((eCategory & EventCategory::Input) != EventCategory::None)
		{
			InputManager::Get().OnEvent(e);
		}
	}

	void Application::OnWindowResize(WindowResizeEvent& event)
	{
	}

	void Application::OnWindowClose(WindowCloseEvent & event)
	{
		m_IsRunning = false;
	}
}

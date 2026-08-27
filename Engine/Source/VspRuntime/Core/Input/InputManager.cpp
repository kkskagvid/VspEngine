#include "RuntimePCH.h"

#include "Core/Events/InputEvents.h"
#include "Core/Input/InputManager.h"

namespace Vsp
{
	InputManager& InputManager::Get()
	{
		static InputManager s_Instance;
		return s_Instance;
	}

	void InputManager::BeginFrame()
	{
		// Accumulation happens during the frame; nothing to reset here.
	}

	void InputManager::EndFrame()
	{
		// Clear per-frame edge state so the next frame starts fresh.
		for (int32_t nKeyIndex = 0; nKeyIndex < k_nKeyStateCount; ++nKeyIndex)
		{
			m_bKeyPressedEdges[nKeyIndex] = false;
			m_bKeyReleasedEdges[nKeyIndex] = false;
		}

		for (int32_t nButtonIndex = 0; nButtonIndex < k_nMouseButtonCount; ++nButtonIndex)
		{
			m_bMouseButtonPressedEdges[nButtonIndex] = false;
			m_bMouseButtonReleasedEdges[nButtonIndex] = false;
		}

		m_fMouseDeltaX = 0.0f;
		m_fMouseDeltaY = 0.0f;
		m_fScrollX = 0.0f;
		m_fScrollY = 0.0f;

		// Drop any unread typed characters.
		m_nTypedCharacterCount = 0;
		m_nTypedCharacterReadIndex = 0;
	}

	void InputManager::OnEvent(Event& eEvent)
	{
		switch (eEvent.GetEventType())
		{
		case EventType::KeyPressed:
		{
			KeyPressedEvent& eKeyEvent = static_cast<KeyPressedEvent&>(eEvent);
			HandleKeyPressed(eKeyEvent.KeyCode, eKeyEvent.RepeatCount);
			break;
		}

		case EventType::KeyReleased:
		{
			KeyReleasedEvent& eKeyEvent = static_cast<KeyReleasedEvent&>(eEvent);
			HandleKeyReleased(eKeyEvent.KeyCode);
			break;
		}

		case EventType::KeyTyped:
		{
			KeyTypedEvent& eKeyEvent = static_cast<KeyTypedEvent&>(eEvent);
			HandleCharacterTyped(eKeyEvent.Codepoint);
			break;
		}

		case EventType::MouseMoved:
		{
			MouseMovedEvent& eMouseEvent = static_cast<MouseMovedEvent&>(eEvent);
			HandleMouseMoved(eMouseEvent.X, eMouseEvent.Y);
			break;
		}

		case EventType::MouseButtonPressed:
		{
			MouseButtonPressedEvent& eMouseEvent = static_cast<MouseButtonPressedEvent&>(eEvent);
			HandleMouseButtonPressed(eMouseEvent.Button);
			break;
		}

		case EventType::MouseButtonReleased:
		{
			MouseButtonReleasedEvent& eMouseEvent = static_cast<MouseButtonReleasedEvent&>(eEvent);
			HandleMouseButtonReleased(eMouseEvent.Button);
			break;
		}

		case EventType::MouseScrolled:
		{
			MouseScrolledEvent& eMouseEvent = static_cast<MouseScrolledEvent&>(eEvent);
			HandleMouseScrolled(eMouseEvent.XOffset, eMouseEvent.YOffset);
			break;
		}

		default:
			break;
		}
	}

	// =========================================================================
	// Keyboard
	// =========================================================================

	bool InputManager::IsKeyDown(KeyCode eKey) const
	{
		const int32_t nKeyCode = static_cast<int32_t>(eKey);
		return IsValidKeyIndex(nKeyCode) && m_bKeyDownStates[nKeyCode];
	}

	bool InputManager::WasKeyPressed(KeyCode eKey) const
	{
		const int32_t nKeyCode = static_cast<int32_t>(eKey);
		return IsValidKeyIndex(nKeyCode) && m_bKeyPressedEdges[nKeyCode];
	}

	bool InputManager::WasKeyReleased(KeyCode eKey) const
	{
		const int32_t nKeyCode = static_cast<int32_t>(eKey);
		return IsValidKeyIndex(nKeyCode) && m_bKeyReleasedEdges[nKeyCode];
	}

	void InputManager::HandleKeyPressed(int32_t nKeyCode, int32_t nRepeatCount)
	{
		if (!IsValidKeyIndex(nKeyCode))
		{
			return;
		}

		// Repeat messages only refresh the edge when the key was not held before.
		if (!m_bKeyDownStates[nKeyCode])
		{
			m_bKeyDownStates[nKeyCode] = true;
			m_bKeyPressedEdges[nKeyCode] = true;
		}
	}

	void InputManager::HandleKeyReleased(int32_t nKeyCode)
	{
		if (!IsValidKeyIndex(nKeyCode))
		{
			return;
		}

		if (m_bKeyDownStates[nKeyCode])
		{
			m_bKeyDownStates[nKeyCode] = false;
			m_bKeyReleasedEdges[nKeyCode] = true;
		}
	}

	// =========================================================================
	// Mouse
	// =========================================================================

	bool InputManager::IsMouseButtonDown(int32_t nButton) const
	{
		return IsValidButtonIndex(nButton) && m_bMouseButtonDownStates[nButton];
	}

	bool InputManager::WasMouseButtonPressed(int32_t nButton) const
	{
		return IsValidButtonIndex(nButton) && m_bMouseButtonPressedEdges[nButton];
	}

	bool InputManager::WasMouseButtonReleased(int32_t nButton) const
	{
		return IsValidButtonIndex(nButton) && m_bMouseButtonReleasedEdges[nButton];
	}

	void InputManager::HandleMouseMoved(float fPositionX, float fPositionY)
	{
		if (m_bHasMousePosition)
		{
			m_fMouseDeltaX += fPositionX - m_fMousePositionX;
			m_fMouseDeltaY += fPositionY - m_fMousePositionY;
		}
		else
		{
			m_bHasMousePosition = true;
		}

		m_fMousePositionX = fPositionX;
		m_fMousePositionY = fPositionY;
	}

	void InputManager::HandleMouseButtonPressed(int32_t nButton)
	{
		if (!IsValidButtonIndex(nButton))
		{
			return;
		}

		if (!m_bMouseButtonDownStates[nButton])
		{
			m_bMouseButtonDownStates[nButton] = true;
			m_bMouseButtonPressedEdges[nButton] = true;
		}
	}

	void InputManager::HandleMouseButtonReleased(int32_t nButton)
	{
		if (!IsValidButtonIndex(nButton))
		{
			return;
		}

		if (m_bMouseButtonDownStates[nButton])
		{
			m_bMouseButtonDownStates[nButton] = false;
			m_bMouseButtonReleasedEdges[nButton] = true;
		}
	}

	void InputManager::HandleMouseScrolled(float fOffsetX, float fOffsetY)
	{
		m_fScrollX += fOffsetX;
		m_fScrollY += fOffsetY;
	}

	// =========================================================================
	// Typed characters
	// =========================================================================

	void InputManager::HandleCharacterTyped(uint32_t uCodePoint)
	{
		if (m_nTypedCharacterCount >= k_nTypedCharacterQueueCapacity)
		{
			return;   // Queue full: drop the oldest-style behaviour would lose input, keep simple.
		}

		const uint32_t nWriteIndex = (m_nTypedCharacterReadIndex + m_nTypedCharacterCount) % k_nTypedCharacterQueueCapacity;
		m_TypedCharacterQueue[nWriteIndex] = uCodePoint;
		++m_nTypedCharacterCount;
	}

	bool InputManager::PopTypedCharacter(uint32_t& outCodePoint)
	{
		if (m_nTypedCharacterCount == 0)
		{
			return false;
		}

		outCodePoint = m_TypedCharacterQueue[m_nTypedCharacterReadIndex];
		m_nTypedCharacterReadIndex = (m_nTypedCharacterReadIndex + 1) % k_nTypedCharacterQueueCapacity;
		--m_nTypedCharacterCount;
		return true;
	}
}

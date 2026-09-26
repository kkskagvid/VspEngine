#include "RuntimePCH.h"

#include "Common/PlatformMisc.h"
#include "Core/Events/InputEvents.h"
#include "Core/Input/InputManager.h"
#include "Core/Logging/Log.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "Input";

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
		for (int32 nKeyIndex = 0; nKeyIndex < k_nKeyStateCount; ++nKeyIndex)
		{
			m_bKeyPressedEdges[nKeyIndex] = false;
			m_bKeyReleasedEdges[nKeyIndex] = false;
		}

		for (int32 nButtonIndex = 0; nButtonIndex < k_nMouseButtonCount; ++nButtonIndex)
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

		// The pointer of a locked cursor goes back to the middle of the window once
		// the frame has read the movement it produced. Doing it HERE - after the
		// scripts have run and after the frame has been recorded - is what keeps a
		// frame's movement whole: the delta a script read came from the player's
		// hand, and the jump back to the middle happens after nobody is looking.
		ApplyCursorMode();
		if (m_eCursorMode == CursorMode::Locked)
		{
			int32 nCentreX = 0;
			int32 nCentreY = 0;
			if (PlatformMisc::CentreCursorInWindow(m_pCursorWindowHandle, nCentreX, nCentreY))
			{
				m_fCursorCentreX = static_cast<float>(nCentreX);
				m_fCursorCentreY = static_cast<float>(nCentreY);
				m_bHasCursorCentre = true;
			}
		}
	}

	// =========================================================================
	// Cursor
	// =========================================================================

	void InputManager::SetCursorMode(CursorMode eCursorMode)
	{
		if (m_eCursorMode == eCursorMode)
		{
			return;
		}

		m_eCursorMode = eCursorMode;
		m_bHasCursorCentre = false;
		ApplyCursorMode();
	}

	void InputManager::SetCursorLockAllowed(bool bIsCursorLockAllowed)
	{
		if (m_bIsCursorLockAllowed == bIsCursorLockAllowed)
		{
			return;
		}

		m_bIsCursorLockAllowed = bIsCursorLockAllowed;
		ApplyCursorMode();
	}

	void InputManager::ApplyCursorMode()
	{
		// Three things have to hold before the pointer may be taken: the game asked
		// for it, the run allows it, and the window is the one the player's input
		// goes to. A window in the background does not own the pointer, so
		// alt-tabbing away gives it back and returning takes it again - which the
		// per-frame call below does by itself.
		const bool bOwnsPointer = PlatformMisc::IsWindowFocused(m_pCursorWindowHandle);
		const bool bIsLocked = bOwnsPointer && m_bIsCursorLockAllowed && (m_eCursorMode == CursorMode::Locked);

		// A game that asked for the pointer and did not get it is told why once,
		// rather than being left to wonder why its cursor never went away.
		if (!m_bIsCursorLockAllowed && m_eCursorMode == CursorMode::Locked && !m_bHasReportedLockRefusal)
		{
			m_bHasReportedLockRefusal = true;
			LOG_INFO(kLogTag, "The game asked for a locked cursor, but this run leaves the pointer to the user (--no-cursor-lock).");
		}

		// A locked cursor is a hidden one: the pointer still moves and still
		// reports where it is, it is simply not drawn over the game.
		PlatformMisc::SetCursorVisible(!bIsLocked);

		if (!bOwnsPointer || m_eCursorMode == CursorMode::Visible)
		{
			PlatformMisc::ReleaseCursorConfinement();
		}
		else
		{
			PlatformMisc::ConfineCursorToWindow(m_pCursorWindowHandle);
		}

		// This runs every frame, so only the transitions are worth a line - and
		// they are exactly what a run of the demo is checked against.
		if (bIsLocked != m_bIsCursorLocked)
		{
			m_bIsCursorLocked = bIsLocked;
			if (bIsLocked)
			{
				LOG_INFO(kLogTag, "Cursor locked: the pointer is hidden and held at the centre of the window.");
			}
			else
			{
				LOG_INFO(kLogTag, "Cursor released: the pointer is the user's again.");
			}
		}
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
		const int32 nKeyCode = static_cast<int32>(eKey);
		return IsValidKeyIndex(nKeyCode) && m_bKeyDownStates[nKeyCode];
	}

	bool InputManager::WasKeyPressed(KeyCode eKey) const
	{
		const int32 nKeyCode = static_cast<int32>(eKey);
		return IsValidKeyIndex(nKeyCode) && m_bKeyPressedEdges[nKeyCode];
	}

	bool InputManager::WasKeyReleased(KeyCode eKey) const
	{
		const int32 nKeyCode = static_cast<int32>(eKey);
		return IsValidKeyIndex(nKeyCode) && m_bKeyReleasedEdges[nKeyCode];
	}

	void InputManager::HandleKeyPressed(int32 nKeyCode, int32 nRepeatCount)
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

	void InputManager::HandleKeyReleased(int32 nKeyCode)
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

	bool InputManager::IsMouseButtonDown(int32 nButton) const
	{
		return IsValidButtonIndex(nButton) && m_bMouseButtonDownStates[nButton];
	}

	bool InputManager::WasMouseButtonPressed(int32 nButton) const
	{
		return IsValidButtonIndex(nButton) && m_bMouseButtonPressedEdges[nButton];
	}

	bool InputManager::WasMouseButtonReleased(int32 nButton) const
	{
		return IsValidButtonIndex(nButton) && m_bMouseButtonReleasedEdges[nButton];
	}

	void InputManager::HandleMouseMoved(float fPositionX, float fPositionY)
	{
		// While the cursor is locked the engine puts the pointer back at the centre
		// of the window after every frame, and the system reports that jump like any
		// other movement. It is not one: a move that lands exactly on the place the
		// engine chose is the engine's own doing, so it updates the position without
		// turning anything. A hand does not land on that same pixel by accident, and
		// the next real movement is measured from there in either case.
		const bool bIsOwnRecentre = m_bHasCursorCentre &&
			(m_eCursorMode == CursorMode::Locked) &&
			(fPositionX == m_fCursorCentreX) &&
			(fPositionY == m_fCursorCentreY);

		if (!m_bHasMousePosition)
		{
			m_bHasMousePosition = true;
		}
		else if (!bIsOwnRecentre)
		{
			m_fMouseDeltaX += fPositionX - m_fMousePositionX;
			m_fMouseDeltaY += fPositionY - m_fMousePositionY;
		}

		m_fMousePositionX = fPositionX;
		m_fMousePositionY = fPositionY;
	}

	void InputManager::HandleMouseButtonPressed(int32 nButton)
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

	void InputManager::HandleMouseButtonReleased(int32 nButton)
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

	void InputManager::HandleCharacterTyped(uint32 uCodePoint)
	{
		if (m_nTypedCharacterCount >= k_nTypedCharacterQueueCapacity)
		{
			return;   // Queue full: drop the oldest-style behaviour would lose input, keep simple.
		}

		const uint32 nWriteIndex = (m_nTypedCharacterReadIndex + m_nTypedCharacterCount) % k_nTypedCharacterQueueCapacity;
		m_TypedCharacterQueue[nWriteIndex] = uCodePoint;
		++m_nTypedCharacterCount;
	}

	bool InputManager::PopTypedCharacter(uint32& outCodePoint)
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

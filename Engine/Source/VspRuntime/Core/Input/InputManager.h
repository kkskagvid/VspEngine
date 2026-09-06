#pragma once

#include "Core/Core.h"
#include "Core/Events/Event.h"
#include "Core/Input/KeyCode.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// InputManager
	// -------------------------------------------------------------------------
	// Engine-wide input state. Window messages arrive as events, this class
	// turns them into pollable state:
	//   - IsKeyDown / WasKeyPressed / WasKeyReleased   (keyboard)
	//   - mouse button equivalents, cursor position, movement delta, wheel
	//   - a queue of typed Unicode characters
	// Edge state (pressed/released), the mouse delta and the wheel accumulate
	// during a frame and are cleared by EndFrame(), so scripts always see one
	// consistent snapshot per frame.
	// -------------------------------------------------------------------------
	class RUNTIME_API InputManager
	{
	public:
		static constexpr int32 k_nKeyStateCount = 256;
		static constexpr int32 k_nMouseButtonCount = 8;
		static constexpr int32 k_nTypedCharacterQueueCapacity = 64;

		static InputManager& Get();

		// Frame lifecycle: call BeginFrame before processing window messages
		// and EndFrame after rendering.
		void BeginFrame();
		void EndFrame();

		// Forwards engine events into the input state.
		void OnEvent(Event& eEvent);

		// -------- Keyboard --------
		bool IsKeyDown(KeyCode eKey) const;
		bool WasKeyPressed(KeyCode eKey) const;
		bool WasKeyReleased(KeyCode eKey) const;

		// -------- Mouse --------
		bool IsMouseButtonDown(int32 nButton) const;
		bool WasMouseButtonPressed(int32 nButton) const;
		bool WasMouseButtonReleased(int32 nButton) const;

		float GetMousePositionX() const { return m_fMousePositionX; }
		float GetMousePositionY() const { return m_fMousePositionY; }
		float GetMouseDeltaX() const { return m_fMouseDeltaX; }
		float GetMouseDeltaY() const { return m_fMouseDeltaY; }
		float GetScrollX() const { return m_fScrollX; }
		float GetScrollY() const { return m_fScrollY; }

		// -------- Typed characters --------
		// Pops the oldest typed code point; returns false when the queue is empty.
		bool PopTypedCharacter(uint32& outCodePoint);

	private:
		InputManager() = default;

		void HandleKeyPressed(int32 nKeyCode, int32 nRepeatCount);
		void HandleKeyReleased(int32 nKeyCode);
		void HandleMouseMoved(float fPositionX, float fPositionY);
		void HandleMouseButtonPressed(int32 nButton);
		void HandleMouseButtonReleased(int32 nButton);
		void HandleMouseScrolled(float fOffsetX, float fOffsetY);
		void HandleCharacterTyped(uint32 uCodePoint);

		static bool IsValidKeyIndex(int32 nKeyCode) { return nKeyCode >= 0 && nKeyCode < k_nKeyStateCount; }
		static bool IsValidButtonIndex(int32 nButton) { return nButton >= 0 && nButton < k_nMouseButtonCount; }

		// Keyboard state: held / pressed-this-frame / released-this-frame.
		bool m_bKeyDownStates[k_nKeyStateCount] = {};
		bool m_bKeyPressedEdges[k_nKeyStateCount] = {};
		bool m_bKeyReleasedEdges[k_nKeyStateCount] = {};

		// Mouse button state: held / pressed-this-frame / released-this-frame.
		bool m_bMouseButtonDownStates[k_nMouseButtonCount] = {};
		bool m_bMouseButtonPressedEdges[k_nMouseButtonCount] = {};
		bool m_bMouseButtonReleasedEdges[k_nMouseButtonCount] = {};

		float m_fMousePositionX = 0.0f;
		float m_fMousePositionY = 0.0f;
		float m_fMouseDeltaX = 0.0f;
		float m_fMouseDeltaY = 0.0f;
		float m_fScrollX = 0.0f;
		float m_fScrollY = 0.0f;
		bool m_bHasMousePosition = false;

		// Ring buffer of typed Unicode code points.
		uint32 m_TypedCharacterQueue[k_nTypedCharacterQueueCapacity] = {};
		uint32 m_nTypedCharacterCount = 0;
		uint32 m_nTypedCharacterReadIndex = 0;
	};
}

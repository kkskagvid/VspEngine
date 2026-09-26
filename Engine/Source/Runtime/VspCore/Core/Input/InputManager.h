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

		// -----------------------------------------------------------------
		// Cursor
		// -----------------------------------------------------------------
		// How the pointer behaves while the game is played. The engine owns the
		// pointer's freedom of movement, which is what a camera the mouse turns
		// needs: while the cursor is LOCKED the pointer is hidden and put back at
		// the centre of the window after every frame, so what it reports is a
		// movement of the HAND and never runs out of screen.
		// -----------------------------------------------------------------
		enum class CursorMode : uint32
		{
			// The ordinary pointer: visible, and free to leave the window.
			Visible = 0,

			// Visible, but held inside the window's client area.
			Confined = 1,

			// Hidden, and recentred after every frame: the mode a mouse look uses.
			Locked = 2,
		};

		static InputManager& Get();

		// Frame lifecycle: call BeginFrame before processing window messages
		// and EndFrame after rendering.
		void BeginFrame();
		void EndFrame();

		// -------- Cursor --------
		// The window the pointer belongs to. The host sets it once, when the
		// window exists; without one the cursor calls do nothing at all.
		void SetCursorWindowHandle(void* pWindowHandle) { m_pCursorWindowHandle = pWindowHandle; }
		void* GetCursorWindowHandle() const { return m_pCursorWindowHandle; }

		CursorMode GetCursorMode() const { return m_eCursorMode; }
		void SetCursorMode(CursorMode eCursorMode);

		// True while the pointer is hidden and recentred every frame.
		bool IsCursorLocked() const { return m_eCursorMode == CursorMode::Locked; }

		// Whether a game may take the pointer at all. True for a player at the
		// controls; a run that must leave the machine alone - an automated test on
		// someone's desktop - turns it off, and a game asking for a locked cursor
		// then keeps the ordinary one (which the engine says once, in the log).
		bool IsCursorLockAllowed() const { return m_bIsCursorLockAllowed; }
		void SetCursorLockAllowed(bool bIsCursorLockAllowed);

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

		// Pushes the current mode onto the platform: the pointer's visibility and
		// whether it is held inside the window. Called when the mode changes and
		// again every frame, so a window that was in the background catches up by
		// itself as soon as it is not.
		void ApplyCursorMode();

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

		// Window the pointer belongs to, and how it is allowed to behave.
		void* m_pCursorWindowHandle = nullptr;
		CursorMode m_eCursorMode = CursorMode::Visible;

		// Where the engine last put the pointer while the cursor was locked. A move
		// that reports exactly this place is the engine's own recentring rather
		// than a movement of the hand, and turns nothing.
		float m_fCursorCentreX = 0.0f;
		float m_fCursorCentreY = 0.0f;
		bool m_bHasCursorCentre = false;

		// What the pointer is doing right now, so the mode is only pushed onto the
		// platform - and reported - when it actually changes.
		bool m_bIsCursorLocked = false;

		// Host policy: whether a game is allowed to take the pointer.
		bool m_bIsCursorLockAllowed = true;

		// True once the engine has explained that it kept the pointer because the
		// run does not allow taking it, so the line is written once and not per frame.
		bool m_bHasReportedLockRefusal = false;

		// Ring buffer of typed Unicode code points.
		uint32 m_TypedCharacterQueue[k_nTypedCharacterQueueCapacity] = {};
		uint32 m_nTypedCharacterCount = 0;
		uint32 m_nTypedCharacterReadIndex = 0;
	};
}

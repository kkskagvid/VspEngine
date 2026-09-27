#pragma once

#include "Core/Core.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// EngineFrame
	// -------------------------------------------------------------------------
	// The single owner of ONE engine frame.
	//
	// A frame is more than the graphics commands recorded inside it: it is also
	// the window in which window messages become input, in which scripts run and
	// the physics world steps, and in which the render pipeline records. Those are
	// three different subsystems, each with its own idea of "begin" and "end":
	//
	//   * GraphicsSystem / RenderCore own the RECORDED COMMAND LIST of the frame
	//     (RenderCore::BeginFrame / EndFrame, closed by the managed submit);
	//   * InputManager owns the INPUT ACCUMULATION WINDOW of the frame
	//     (BeginInputFrame / EndInputFrame);
	//   * this class owns the FRAME ITSELF, and is what the host calls.
	//
	// The two calls, and what they do:
	//
	//   BeginFrame()
	//     Starts the frame. The frame's command list empties (RenderCore) and the
	//     input accumulation window opens (InputManager). Window messages are
	//     pumped AFTER this call, which is what makes everything they produce
	//     belong to THIS frame. Nothing else may open a frame: a pipeline records
	//     INTO the frame it is handed and never opens or closes one.
	//
	//   EndFrame()
	//     Ends the frame. The input the frame accumulated is put to bed exactly
	//     once - pressed/released edges, the mouse delta, the wheel and the typed
	//     character queue are cleared, and a locked cursor is recentred - so a
	//     script can never read the same key press twice and a game cannot forget
	//     to end the frame. The recorded command list is NOT closed here: the
	//     managed render pipeline closes it when it submits, and the backend plays
	//     it back before this call is reached.
	//
	// Order inside a frame (the host's main loop, Core/Engine.cpp):
	//
	//   BeginFrame -> window messages -> scripts -> physics -> frame driver
	//   (renders and submits the frame) -> backend render and present -> EndFrame
	//
	// Threading: an engine frame is single-threaded. Everything above runs on the
	// thread that calls BeginFrame, which is also the thread that owns every
	// engine service (see Core/EngineServices.h).
	//
	// All errors are logged through the Log module; nothing returns a failure and
	// nothing throws.
	// -------------------------------------------------------------------------
	class RUNTIME_API EngineFrame
	{
	public:
		static EngineFrame& Get();

		// Opens the frame. Call once per frame, before window messages are pumped.
		void BeginFrame();

		// Closes the frame. Call once per frame, after the frame was rendered.
		void EndFrame();

		// True between BeginFrame() and EndFrame() (diagnostics / tests).
		bool IsFrameOpen() const { return m_bIsFrameOpen; }

	private:
		EngineFrame() = default;

		// The registry in Core/EngineServices.h owns this service's storage and
		// lifetime, so it has to be able to construct it.
		friend class EngineServices;

		bool m_bIsFrameOpen = false;
	};
}

#include "RuntimePCH.h"

#include "Core/EngineFrame.h"
#include "Core/EngineServices.h"
#include "Core/Input/InputManager.h"
#include "Core/Logging/Log.h"
#include "Graphics/RenderCore.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "EngineFrame";

	EngineFrame& EngineFrame::Get()
	{
		// The registry owns this service: it is created here on first use,
		// reports a lookup from any thread but the one that created it, and is
		// destroyed explicitly by EngineServices::ShutdownAll().
		return EngineServices::GetService<EngineFrame>("EngineFrame");
	}

	// -------------------------------------------------------------------------
	// Frame lifetime
	// -------------------------------------------------------------------------

	void EngineFrame::BeginFrame()
	{
		if (m_bIsFrameOpen)
		{
			// Opening a frame that is already open would throw away the commands
			// recorded so far and restart the input window mid-frame, so it is
			// reported and ignored instead.
			LOG_WARNING(kLogTag, "BeginFrame was called while a frame is still open; the call is ignored. "
				"Every frame is BeginFrame ... EndFrame, once.");
			return;
		}

		m_bIsFrameOpen = true;

		// The frame's command list starts empty, and the window in which input is
		// accumulated opens here - the host pumps window messages right after this
		// call, so everything they produce belongs to this frame.
		RenderCore::Get().BeginFrame();
		InputManager::Get().BeginInputFrame();
	}

	void EngineFrame::EndFrame()
	{
		if (!m_bIsFrameOpen)
		{
			LOG_WARNING(kLogTag, "EndFrame was called without BeginFrame; the call is ignored.");
			return;
		}

		m_bIsFrameOpen = false;

		// The frame is over: the input it accumulated is cleared once, for every
		// frame, and a locked cursor is put back at the centre of the window after
		// everyone who reads the mouse has read it.
		InputManager::Get().EndInputFrame();
	}
}

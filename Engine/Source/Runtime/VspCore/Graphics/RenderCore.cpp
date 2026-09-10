#include "RuntimePCH.h"

#include "Core/Logging/Log.h"
#include "Graphics/RenderCore.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "RenderCore";

	RenderCore& RenderCore::Get()
	{
		static RenderCore s_Instance;
		return s_Instance;
	}

	void RenderCore::BeginFrame()
	{
		m_bFrameBegun = true;
		m_bFrameEnded = false;
		m_DrawCommands.Clear();
	}

	void RenderCore::SetClearColor(float fColorR, float fColorG, float fColorB, float fColorA)
	{
		m_ClearColor.fColorR = fColorR;
		m_ClearColor.fColorG = fColorG;
		m_ClearColor.fColorB = fColorB;
		m_ClearColor.fColorA = fColorA;
	}

	void RenderCore::DrawTriangle(float fPositionX, float fPositionY, int32 nColorMode)
	{
		if (m_DrawCommands.GetSize() >= k_nMaxDrawCommandCount)
		{
			LOG_WARNING(kLogTag, "Frame draw command limit ({}) reached; further draws are dropped.",
				k_nMaxDrawCommandCount);
			return;
		}

		TriangleDrawCommand command;
		command.fPositionX = fPositionX;
		command.fPositionY = fPositionY;
		command.nColorMode = nColorMode;
		m_DrawCommands.Add(command);
	}

	void RenderCore::EndFrame()
	{
		if (!m_bFrameBegun)
		{
			// EndFrame without BeginFrame is ignored: the frame stays invalid
			// and the renderer falls back to its default frame.
			LOG_WARNING(kLogTag, "EndFrame called without BeginFrame; the frame is ignored.");
			return;
		}

		m_bFrameEnded = true;
	}
}

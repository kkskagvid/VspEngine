#include "RuntimePCH.h"

#include <cstring>

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

	// -------------------------------------------------------------------------
	// Frame lifetime
	// -------------------------------------------------------------------------

	void RenderCore::BeginFrame()
	{
		m_bFrameBegun = true;
		m_bFrameEnded = false;
		m_bCommandLimitReported = false;
		m_Commands.Clear();
	}

	void RenderCore::EndFrame()
	{
		if (!m_bFrameBegun)
		{
			// EndFrame without BeginFrame is ignored: the frame stays invalid
			// and the backend falls back to its default frame.
			LOG_WARNING(kLogTag, "EndFrame called without BeginFrame; the frame is ignored.");
			return;
		}

		m_bFrameEnded = true;
	}

	void RenderCore::ResetCommands()
	{
		m_bFrameBegun = false;
		m_bFrameEnded = false;
		m_bCommandLimitReported = false;
		m_Commands.Clear();
	}

	void RenderCore::SetClearColor(float fColorR, float fColorG, float fColorB, float fColorA)
	{
		m_ClearColor.fColorR = fColorR;
		m_ClearColor.fColorG = fColorG;
		m_ClearColor.fColorB = fColorB;
		m_ClearColor.fColorA = fColorA;
	}

	// -------------------------------------------------------------------------
	// Command recording
	// -------------------------------------------------------------------------

	RhiCommand& RenderCore::AddCommand(RhiCommandType eType)
	{
		if (m_Commands.GetSize() >= k_nMaxRecordedCommandCount)
		{
			if (!m_bCommandLimitReported)
			{
				LOG_WARNING(kLogTag, "Frame command limit ({}) reached; further commands are dropped.",
					k_nMaxRecordedCommandCount);
				m_bCommandLimitReported = true;
			}
			// Hand the caller a scratch command: recording stays valid, the
			// backend never sees it.
			m_DiscardedCommand = RhiCommand();
			m_DiscardedCommand.eType = eType;
			return m_DiscardedCommand;
		}

		RhiCommand command;
		command.eType = eType;
		return m_Commands.Add(command);
	}

	void RenderCore::BeginRenderPass()
	{
		AddCommand(RhiCommandType::BeginRenderPass);
	}

	void RenderCore::EndRenderPass()
	{
		AddCommand(RhiCommandType::EndRenderPass);
	}

	void RenderCore::SetViewport(float fX, float fY, float fWidth, float fHeight)
	{
		RhiCommand& command = AddCommand(RhiCommandType::SetViewport);
		command.fValue[0] = fX;
		command.fValue[1] = fY;
		command.fValue[2] = fWidth;
		command.fValue[3] = fHeight;
	}

	void RenderCore::SetScissor(int32 nX, int32 nY, uint32 uWidth, uint32 uHeight)
	{
		RhiCommand& command = AddCommand(RhiCommandType::SetScissor);
		command.uValueA = static_cast<uint32>(nX);
		command.uValueB = static_cast<uint32>(nY);
		command.uValueC = uWidth;
		command.uValueD = uHeight;
	}

	void RenderCore::BindPipeline(RhiPipelineHandle uPipeline)
	{
		RhiCommand& command = AddCommand(RhiCommandType::BindPipeline);
		command.uResourceHandle = uPipeline;
	}

	void RenderCore::BindVertexBuffer(RhiBufferHandle uVertexBuffer)
	{
		RhiCommand& command = AddCommand(RhiCommandType::BindVertexBuffer);
		command.uResourceHandle = uVertexBuffer;
	}

	void RenderCore::PushConstants(uint32 uShaderStageFlags, uint32 uByteOffset, const void* pData, uint32 uByteCount)
	{
		RhiCommand& command = AddCommand(RhiCommandType::PushConstants);

		uint32 uStoredByteCount = uByteCount;
		if (uStoredByteCount > k_nMaxPushConstantByteCount)
		{
			LOG_WARNING(kLogTag, "Push-constant block of {} bytes exceeds the {} byte limit; it is truncated.",
				uByteCount, k_nMaxPushConstantByteCount);
			uStoredByteCount = k_nMaxPushConstantByteCount;
		}

		command.uValueA = uShaderStageFlags;
		command.uValueB = uByteOffset;
		command.uValueC = uStoredByteCount;
		if (pData != nullptr && uStoredByteCount > 0)
		{
			memcpy(command.PushConstantBytes, pData, uStoredByteCount);
		}
	}

	void RenderCore::Draw(uint32 uVertexCount, uint32 uFirstVertex)
	{
		RhiCommand& command = AddCommand(RhiCommandType::Draw);
		command.uValueA = uVertexCount;
		command.uValueB = uFirstVertex;
	}
}

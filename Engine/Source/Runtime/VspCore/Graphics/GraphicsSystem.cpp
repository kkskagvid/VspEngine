#include "RuntimePCH.h"

#include "Core/EngineServices.h"
#include "Core/Logging/Log.h"
#include "Graphics/GraphicsSystem.h"
#include "Graphics/RenderCore.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "GraphicsSystem";

	GraphicsSystem& GraphicsSystem::Get()
	{
		// The registry owns this service: it is created here on first use,
		// reports a lookup from any thread but the one that created it, and is
		// destroyed explicitly by EngineServices::ShutdownAll().
		return EngineServices::GetService<GraphicsSystem>("GraphicsSystem");
	}

	// -------------------------------------------------------------------------
	// Buffers
	// -------------------------------------------------------------------------

	RhiBufferHandle GraphicsSystem::CreateBuffer(const RhiBufferDescriptor& descriptor)
	{
		if (m_pActiveBackend == nullptr)
		{
			LOG_ERROR(kLogTag, "CreateBuffer: no graphics backend is active.");
			return k_nInvalidRhiHandle;
		}
		if (descriptor.uByteSize == 0)
		{
			LOG_ERROR(kLogTag, "CreateBuffer: a zero-byte buffer is not a valid request.");
			return k_nInvalidRhiHandle;
		}
		return m_pActiveBackend->CreateBuffer(descriptor);
	}

	void GraphicsSystem::DestroyBuffer(RhiBufferHandle uBuffer)
	{
		if (m_pActiveBackend != nullptr)
		{
			m_pActiveBackend->DestroyBuffer(uBuffer);
		}
	}

	bool GraphicsSystem::UpdateBuffer(RhiBufferHandle uBuffer, uint32 uByteOffset, const void* pData, uint32 uByteCount)
	{
		if (m_pActiveBackend == nullptr || pData == nullptr || uByteCount == 0)
		{
			return false;
		}
		return m_pActiveBackend->UpdateBuffer(uBuffer, uByteOffset, pData, uByteCount);
	}

	// -------------------------------------------------------------------------
	// Shaders
	// -------------------------------------------------------------------------

	RhiShaderHandle GraphicsSystem::CreateShader(
		RhiShaderStage eStage,
		const char* pEntryPointName,
		const void* pSpirvCode,
		uint32 uByteCount)
	{
		if (m_pActiveBackend == nullptr)
		{
			LOG_ERROR(kLogTag, "CreateShader: no graphics backend is active.");
			return k_nInvalidRhiHandle;
		}
		if (pEntryPointName == nullptr || *pEntryPointName == '\0')
		{
			LOG_ERROR(kLogTag, "CreateShader: the entry point name is empty.");
			return k_nInvalidRhiHandle;
		}
		if (pSpirvCode == nullptr || uByteCount == 0 || (uByteCount % 4) != 0)
		{
			LOG_ERROR(kLogTag, "CreateShader: the SPIR-V blob must be non-empty and 4-byte aligned.");
			return k_nInvalidRhiHandle;
		}
		return m_pActiveBackend->CreateShader(eStage, pEntryPointName, pSpirvCode, uByteCount);
	}

	void GraphicsSystem::DestroyShader(RhiShaderHandle uShader)
	{
		if (m_pActiveBackend != nullptr)
		{
			m_pActiveBackend->DestroyShader(uShader);
		}
	}

	// -------------------------------------------------------------------------
	// Textures
	// -------------------------------------------------------------------------

	RhiTextureHandle GraphicsSystem::CreateTexture(const RhiTextureDescriptor& descriptor, const void* pPixelDataRgba8)
	{
		if (m_pActiveBackend == nullptr)
		{
			LOG_ERROR(kLogTag, "CreateTexture: no graphics backend is active.");
			return k_nInvalidRhiHandle;
		}
		if (descriptor.uWidth == 0 || descriptor.uHeight == 0 || pPixelDataRgba8 == nullptr)
		{
			LOG_ERROR(kLogTag, "CreateTexture: width, height and pixel data are all required.");
			return k_nInvalidRhiHandle;
		}
		return m_pActiveBackend->CreateTexture(descriptor, pPixelDataRgba8);
	}

	void GraphicsSystem::DestroyTexture(RhiTextureHandle uTexture)
	{
		if (m_pActiveBackend != nullptr)
		{
			m_pActiveBackend->DestroyTexture(uTexture);
		}
	}

	int32 GraphicsSystem::GetTextureBindlessSlot(RhiTextureHandle uTexture) const
	{
		return m_pActiveBackend != nullptr ? m_pActiveBackend->GetTextureBindlessSlot(uTexture) : -1;
	}

	// -------------------------------------------------------------------------
	// Pipelines
	// -------------------------------------------------------------------------

	RhiPipelineHandle GraphicsSystem::CreateGraphicsPipeline(const RhiGraphicsPipelineState& state)
	{
		if (m_pActiveBackend == nullptr)
		{
			LOG_ERROR(kLogTag, "CreateGraphicsPipeline: no graphics backend is active.");
			return k_nInvalidRhiHandle;
		}
		return m_pActiveBackend->CreateGraphicsPipeline(state);
	}

	void GraphicsSystem::DestroyGraphicsPipeline(RhiPipelineHandle uPipeline)
	{
		if (m_pActiveBackend != nullptr)
		{
			m_pActiveBackend->DestroyGraphicsPipeline(uPipeline);
		}
	}

	// -------------------------------------------------------------------------
	// Back buffer
	// -------------------------------------------------------------------------

	uint32 GraphicsSystem::GetBackbufferWidth() const
	{
		uint32 uWidth = 0;
		uint32 uHeight = 0;
		if (m_pActiveBackend != nullptr)
		{
			m_pActiveBackend->GetBackbufferExtent(uWidth, uHeight);
		}
		return uWidth;
	}

	uint32 GraphicsSystem::GetBackbufferHeight() const
	{
		uint32 uWidth = 0;
		uint32 uHeight = 0;
		if (m_pActiveBackend != nullptr)
		{
			m_pActiveBackend->GetBackbufferExtent(uWidth, uHeight);
		}
		return uHeight;
	}

	// -------------------------------------------------------------------------
	// Pipeline state builders
	// -------------------------------------------------------------------------

	GraphicsSystem::PipelineBuilderEntry* GraphicsSystem::FindPipelineBuilder(RhiPipelineBuilderHandle uBuilder)
	{
		if (uBuilder == k_nInvalidRhiHandle || uBuilder > m_PipelineBuilders.GetSize())
		{
			return nullptr;
		}

		PipelineBuilderEntry& entry = m_PipelineBuilders[uBuilder - 1u];
		return entry.bIsActive ? &entry : nullptr;
	}

	const GraphicsSystem::PipelineBuilderEntry* GraphicsSystem::FindPipelineBuilder(RhiPipelineBuilderHandle uBuilder) const
	{
		return const_cast<GraphicsSystem*>(this)->FindPipelineBuilder(uBuilder);
	}

	RhiPipelineBuilderHandle GraphicsSystem::CreatePipelineBuilder()
	{
		// Recycle a released builder before growing the table.
		for (size_t nBuilderIndex = 0; nBuilderIndex < m_PipelineBuilders.GetSize(); ++nBuilderIndex)
		{
			if (!m_PipelineBuilders[nBuilderIndex].bIsActive)
			{
				PipelineBuilderEntry& entry = m_PipelineBuilders[nBuilderIndex];
				entry.bIsActive = true;
				entry.State = RhiGraphicsPipelineState();
				return static_cast<RhiPipelineBuilderHandle>(nBuilderIndex + 1u);
			}
		}

		if (m_PipelineBuilders.GetSize() >= k_nMaxPipelineBuilderCount)
		{
			LOG_ERROR(kLogTag, "CreatePipelineBuilder: the {} builder limit is reached.", k_nMaxPipelineBuilderCount);
			return k_nInvalidRhiHandle;
		}

		PipelineBuilderEntry entry;
		entry.bIsActive = true;
		m_PipelineBuilders.Add(entry);
		return static_cast<RhiPipelineBuilderHandle>(m_PipelineBuilders.GetSize());
	}

	void GraphicsSystem::DestroyPipelineBuilder(RhiPipelineBuilderHandle uBuilder)
	{
		PipelineBuilderEntry* pEntry = FindPipelineBuilder(uBuilder);
		if (pEntry != nullptr)
		{
			pEntry->bIsActive = false;
			pEntry->State = RhiGraphicsPipelineState();
		}
	}

	void GraphicsSystem::PipelineBuilderSetShader(
		RhiPipelineBuilderHandle uBuilder,
		RhiShaderStage eStage,
		RhiShaderHandle uShader)
	{
		PipelineBuilderEntry* pEntry = FindPipelineBuilder(uBuilder);
		if (pEntry == nullptr)
		{
			return;
		}

		if (eStage == RhiShaderStage::Vertex)
		{
			pEntry->State.uVertexShader = uShader;
		}
		else
		{
			pEntry->State.uFragmentShader = uShader;
		}
	}

	void GraphicsSystem::PipelineBuilderSetVertexStride(RhiPipelineBuilderHandle uBuilder, uint32 uVertexStride)
	{
		PipelineBuilderEntry* pEntry = FindPipelineBuilder(uBuilder);
		if (pEntry != nullptr)
		{
			pEntry->State.uVertexStride = uVertexStride;
		}
	}

	void GraphicsSystem::PipelineBuilderAddVertexAttribute(
		RhiPipelineBuilderHandle uBuilder,
		int32 nShaderLocation,
		uint32 uComponentCount,
		uint32 uByteOffset)
	{
		PipelineBuilderEntry* pEntry = FindPipelineBuilder(uBuilder);
		if (pEntry == nullptr)
		{
			return;
		}

		if (uComponentCount < 1u || uComponentCount > 4u)
		{
			LOG_ERROR(kLogTag, "PipelineBuilderAddVertexAttribute: component count {} is out of range.", uComponentCount);
			return;
		}

		RhiGraphicsPipelineState& state = pEntry->State;
		if (state.uVertexAttributeCount >= k_nMaxPipelineVertexAttributeCount)
		{
			LOG_ERROR(kLogTag, "PipelineBuilderAddVertexAttribute: the {} attribute limit is reached.",
				k_nMaxPipelineVertexAttributeCount);
			return;
		}

		RhiVertexAttribute& attribute = state.VertexAttributes[state.uVertexAttributeCount];
		attribute.nShaderLocation = nShaderLocation;
		attribute.uComponentCount = uComponentCount;
		attribute.uByteOffset = uByteOffset;
		++state.uVertexAttributeCount;
	}

	void GraphicsSystem::PipelineBuilderSetTopology(
		RhiPipelineBuilderHandle uBuilder,
		RhiPrimitiveTopology eTopology)
	{
		PipelineBuilderEntry* pEntry = FindPipelineBuilder(uBuilder);
		if (pEntry != nullptr)
		{
			pEntry->State.eTopology = eTopology;
		}
	}

	void GraphicsSystem::PipelineBuilderSetCullMode(RhiPipelineBuilderHandle uBuilder, RhiCullMode eCullMode)
	{
		PipelineBuilderEntry* pEntry = FindPipelineBuilder(uBuilder);
		if (pEntry != nullptr)
		{
			pEntry->State.eCullMode = eCullMode;
		}
	}

	void GraphicsSystem::PipelineBuilderSetDepthState(
		RhiPipelineBuilderHandle uBuilder,
		bool bDepthTestEnabled,
		bool bDepthWriteEnabled,
		RhiCompareOperation eDepthCompare)
	{
		PipelineBuilderEntry* pEntry = FindPipelineBuilder(uBuilder);
		if (pEntry != nullptr)
		{
			pEntry->State.bDepthTestEnabled = bDepthTestEnabled;
			pEntry->State.bDepthWriteEnabled = bDepthWriteEnabled;
			pEntry->State.eDepthCompare = eDepthCompare;
		}
	}

	void GraphicsSystem::PipelineBuilderSetBlendEnabled(RhiPipelineBuilderHandle uBuilder, bool bBlendEnabled)
	{
		PipelineBuilderEntry* pEntry = FindPipelineBuilder(uBuilder);
		if (pEntry != nullptr)
		{
			pEntry->State.bBlendEnabled = bBlendEnabled;
		}
	}

	void GraphicsSystem::PipelineBuilderSetPushConstantByteCount(
		RhiPipelineBuilderHandle uBuilder,
		uint32 uPushConstantByteCount)
	{
		PipelineBuilderEntry* pEntry = FindPipelineBuilder(uBuilder);
		if (pEntry == nullptr)
		{
			return;
		}

		if (uPushConstantByteCount > k_nMaxPushConstantByteCount)
		{
			LOG_ERROR(kLogTag, "PipelineBuilderSetPushConstantByteCount: {} bytes exceed the {} byte limit.",
				uPushConstantByteCount, k_nMaxPushConstantByteCount);
			return;
		}
		pEntry->State.uPushConstantByteCount = uPushConstantByteCount;
	}

	// The FRAME of the engine - when it starts, what accumulates in it, when it
	// ends - is owned by EngineFrame (Core/EngineFrame.h): the graphics system
	// owns the recorded command list of that frame (RenderCore) and the backend
	// that plays it back, and nothing else. Opening or closing a frame from here
	// would give the frame two owners, which is exactly what the two
	// BeginFrame/EndFrame pairs of the input and the command list used to blur.

	RhiPipelineHandle GraphicsSystem::BuildPipelineFromBuilder(RhiPipelineBuilderHandle uBuilder)
	{
		PipelineBuilderEntry* pEntry = FindPipelineBuilder(uBuilder);
		if (pEntry == nullptr)
		{
			LOG_ERROR(kLogTag, "BuildPipelineFromBuilder: the builder handle is not live.");
			return k_nInvalidRhiHandle;
		}

		const RhiGraphicsPipelineState state = pEntry->State;
		pEntry->bIsActive = false;
		pEntry->State = RhiGraphicsPipelineState();

		// Both stages are required. Vertex input is not: a pipeline whose vertex
		// stage generates its geometry - the sky's fullscreen triangle takes its
		// corners from SV_VertexID - reads no vertex buffer at all, so stating
		// neither a stride nor an attribute is a complete description rather than
		// a forgotten one. A pipeline that states one of the two without the other
		// is a mistake, and is refused.
		const bool bHasVertexInput = state.uVertexStride > 0 || state.uVertexAttributeCount > 0;
		const bool bHasCompleteVertexInput = state.uVertexStride > 0 && state.uVertexAttributeCount > 0;

		if (state.uVertexShader == k_nInvalidRhiHandle ||
			state.uFragmentShader == k_nInvalidRhiHandle ||
			(bHasVertexInput && !bHasCompleteVertexInput))
		{
			LOG_ERROR(kLogTag, "BuildPipelineFromBuilder: both shader stages are required, and vertex input needs "
				"both a stride and at least one vertex attribute (or neither, for a pipeline that reads no vertex buffer).");
			return k_nInvalidRhiHandle;
		}

		return CreateGraphicsPipeline(state);
	}

	void GraphicsSystem::ClearPipelineBuilders()
	{
		m_PipelineBuilders.Clear();
	}
}

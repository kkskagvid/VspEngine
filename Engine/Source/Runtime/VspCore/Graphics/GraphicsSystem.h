#pragma once

#include "Core/Core.h"
#include "Core/Templates/ArrayList.h"
#include "Graphics/IGraphics.h"
#include "Graphics/RhiTypes.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// GraphicsSystem
	// -------------------------------------------------------------------------
	// The single native entry point of the wrapped graphics API. The engine
	// registers the live backend here; the NativeExports forward every managed
	// call (resource creation, pipeline building) into it.
	//
	// It also owns the graphics-pipeline builders: because the managed side
	// assembles pipeline state piecewise over the interop boundary (one call
	// per shader, vertex element, topology, ...), the partially built state
	// lives here between the calls and BuildPipelineFromBuilder() turns it into
	// a real pipeline in the backend.
	//
	// Every function logs its own errors and never throws; resource creation
	// returns k_nInvalidRhiHandle when the backend is missing or refused.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList member: header-only template.
	class RUNTIME_API GraphicsSystem
	{
	public:
		static GraphicsSystem& Get();

		// -------- Active backend --------
		// The engine sets the backend once it is initialized and clears it
		// before the backend goes away.
		void SetActiveBackend(IGraphics* pBackend) { m_pActiveBackend = pBackend; }
		IGraphics* GetActiveBackend() const { return m_pActiveBackend; }
		bool HasActiveBackend() const { return m_pActiveBackend != nullptr; }

		// -------- Buffers --------
		RhiBufferHandle CreateBuffer(const RhiBufferDescriptor& descriptor);
		void DestroyBuffer(RhiBufferHandle uBuffer);
		bool UpdateBuffer(RhiBufferHandle uBuffer, uint32 uByteOffset, const void* pData, uint32 uByteCount);

		// -------- Shaders --------
		RhiShaderHandle CreateShader(
			RhiShaderStage eStage,
			const char* pEntryPointName,
			const void* pSpirvCode,
			uint32 uByteCount);
		void DestroyShader(RhiShaderHandle uShader);

		// -------- Textures (bindless slots) --------
		RhiTextureHandle CreateTexture(const RhiTextureDescriptor& descriptor, const void* pPixelDataRgba8);
		void DestroyTexture(RhiTextureHandle uTexture);
		int32 GetTextureBindlessSlot(RhiTextureHandle uTexture) const;

		// -------- Pipelines --------
		RhiPipelineHandle CreateGraphicsPipeline(const RhiGraphicsPipelineState& state);
		void DestroyGraphicsPipeline(RhiPipelineHandle uPipeline);

		// -------- Back buffer --------
		uint32 GetBackbufferWidth() const;
		uint32 GetBackbufferHeight() const;

		// -------- Pipeline state builders --------
		// The state assembles piecewise because every field crosses the interop
		// boundary as its own call; k_nMaxPipelineBuilderCount builders may be
		// open at the same time.
		RhiPipelineBuilderHandle CreatePipelineBuilder();
		void DestroyPipelineBuilder(RhiPipelineBuilderHandle uBuilder);

		void PipelineBuilderSetShader(RhiPipelineBuilderHandle uBuilder, RhiShaderStage eStage, RhiShaderHandle uShader);
		void PipelineBuilderSetVertexStride(RhiPipelineBuilderHandle uBuilder, uint32 uVertexStride);
		void PipelineBuilderAddVertexAttribute(
			RhiPipelineBuilderHandle uBuilder,
			int32 nShaderLocation,
			uint32 uComponentCount,
			uint32 uByteOffset);
		void PipelineBuilderSetTopology(RhiPipelineBuilderHandle uBuilder, RhiPrimitiveTopology eTopology);
		void PipelineBuilderSetBlendEnabled(RhiPipelineBuilderHandle uBuilder, bool bBlendEnabled);
		void PipelineBuilderSetPushConstantByteCount(RhiPipelineBuilderHandle uBuilder, uint32 uPushConstantByteCount);

		// Creates the backend pipeline from the assembled state and releases the
		// builder; returns k_nInvalidRhiHandle when the state is incomplete.
		RhiPipelineHandle BuildPipelineFromBuilder(RhiPipelineBuilderHandle uBuilder);

		// Releases every builder (engine shutdown).
		void ClearPipelineBuilders();

	private:
		GraphicsSystem() = default;

		// One open pipeline-state assembly.
		struct PipelineBuilderEntry
		{
			bool bIsActive = false;
			RhiGraphicsPipelineState State;
		};

		PipelineBuilderEntry* FindPipelineBuilder(RhiPipelineBuilderHandle uBuilder);
		const PipelineBuilderEntry* FindPipelineBuilder(RhiPipelineBuilderHandle uBuilder) const;

		IGraphics* m_pActiveBackend = nullptr;
		ArrayList<PipelineBuilderEntry> m_PipelineBuilders;
	};
#pragma warning(pop)
}

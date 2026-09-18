#pragma once

#include "Core/Core.h"
#include "Graphics/RhiTypes.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// IGraphics
	// -------------------------------------------------------------------------
	// Interface every graphics backend implements. It has two halves:
	//   - the backend lifetime (initialize / resize / present / shutdown), and
	//   - the wrapped graphics API the managed render pipeline drives:
	//     device resource creation (buffers, shaders, textures, pipelines) plus
	//     the command list recorded in RenderCore, which RenderFrame() plays
	//     back into the swapchain command buffer.
	// Implementations never throw and report failures through the engine log
	// (Log) instead of out-parameters: every function logs its own errors
	// internally and returns an invalid handle (0) on failure.
	// -------------------------------------------------------------------------
	class RUNTIME_API IGraphics
	{
	public:
		virtual ~IGraphics() = default;

		// -------- Backend lifetime --------
		virtual bool Initialize(void* pNativeWindowHandle) = 0;
		virtual void Shutdown() = 0;
		virtual void OnWindowResize(uint32 uWidth, uint32 uHeight) = 0;

		// Plays back the frame recorded in RenderCore and presents it.
		virtual bool RenderFrame() = 0;

		// -------- Wrapped graphics API: buffers --------
		virtual RhiBufferHandle CreateBuffer(const RhiBufferDescriptor& descriptor) = 0;
		virtual void DestroyBuffer(RhiBufferHandle uBuffer) = 0;
		virtual bool UpdateBuffer(RhiBufferHandle uBuffer, uint32 uByteOffset, const void* pData, uint32 uByteCount) = 0;

		// -------- Wrapped graphics API: shaders --------
		virtual RhiShaderHandle CreateShader(RhiShaderStage eStage, const void* pSpirvCode, uint32 uByteCount) = 0;
		virtual void DestroyShader(RhiShaderHandle uShader) = 0;

		// -------- Wrapped graphics API: textures (bindless slots) --------
		virtual RhiTextureHandle CreateTexture(const RhiTextureDescriptor& descriptor, const void* pPixelDataRgba8) = 0;
		virtual void DestroyTexture(RhiTextureHandle uTexture) = 0;

		// Bindless array slot the texture occupies; -1 when the handle is not
		// a live texture. Shaders index the bindless array with this value.
		virtual int32 GetTextureBindlessSlot(RhiTextureHandle uTexture) const = 0;

		// -------- Wrapped graphics API: pipelines --------
		virtual RhiPipelineHandle CreateGraphicsPipeline(const RhiGraphicsPipelineState& state) = 0;
		virtual void DestroyGraphicsPipeline(RhiPipelineHandle uPipeline) = 0;

		// -------- Wrapped graphics API: the back buffer --------
		virtual void GetBackbufferExtent(uint32& outWidth, uint32& outHeight) const = 0;
	};
}

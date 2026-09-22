#include "RuntimePCH.h"

#include <cstring>

#include "Classes/Camera.h"
#include "Classes/Scene.h"
#include "Classes/Transform.h"
#include "Graphics/GraphicsSystem.h"
#include "Graphics/RenderCore.h"
#include "Scripting/ScriptExport.h"

// -------------------------------------------------------------------------
// Wrapped-graphics-API exports consumed by managed code (C# -> C++ direction).
// VspEngine.Rendering.RhiApi P/Invokes these exact names from VspCore.dll.
// The managed render pipeline is the only caller: it creates device resources,
// assembles pipeline state and records one frame per engine frame; the active
// graphics backend plays the recorded commands back while it records the
// swapchain command buffer.
// Everything is plain data in/out - no exceptions cross the boundary.
// -------------------------------------------------------------------------

// -------- Buffers --------

// uBufferUsage: 0 = vertex buffer, 1 = index buffer.
CSHARP_EXPORT uint32 VspRhi_CreateBuffer(uint32 uByteSize, int32 nBufferUsage, int32 bIsDynamic)
{
	Vsp::RhiBufferDescriptor descriptor;
	descriptor.uByteSize = uByteSize;
	descriptor.eUsage = static_cast<Vsp::RhiBufferUsage>(nBufferUsage);
	descriptor.bIsDynamic = (bIsDynamic != 0);
	return Vsp::GraphicsSystem::Get().CreateBuffer(descriptor);
}

CSHARP_EXPORT void VspRhi_DestroyBuffer(uint32 uBuffer)
{
	Vsp::GraphicsSystem::Get().DestroyBuffer(uBuffer);
}

CSHARP_EXPORT int32 VspRhi_UpdateBuffer(uint32 uBuffer, uint32 uByteOffset, const void* pData, uint32 uByteCount)
{
	return Vsp::GraphicsSystem::Get().UpdateBuffer(uBuffer, uByteOffset, pData, uByteCount) ? 1 : 0;
}

// -------- Shaders --------

CSHARP_EXPORT uint32 VspRhi_CreateShader(
	int32 nStage,
	const char* pEntryPointNameUtf8,
	const void* pSpirvCode,
	uint32 uByteCount)
{
	return Vsp::GraphicsSystem::Get().CreateShader(
		static_cast<Vsp::RhiShaderStage>(nStage), pEntryPointNameUtf8, pSpirvCode, uByteCount);
}

CSHARP_EXPORT void VspRhi_DestroyShader(uint32 uShader)
{
	Vsp::GraphicsSystem::Get().DestroyShader(uShader);
}

// -------- Textures (bindless slots) --------

CSHARP_EXPORT uint32 VspRhi_CreateTexture(uint32 uWidth, uint32 uHeight, const void* pPixelDataRgba8)
{
	Vsp::RhiTextureDescriptor descriptor;
	descriptor.uWidth = uWidth;
	descriptor.uHeight = uHeight;
	return Vsp::GraphicsSystem::Get().CreateTexture(descriptor, pPixelDataRgba8);
}

CSHARP_EXPORT void VspRhi_DestroyTexture(uint32 uTexture)
{
	Vsp::GraphicsSystem::Get().DestroyTexture(uTexture);
}

CSHARP_EXPORT int32 VspRhi_GetTextureBindlessSlot(uint32 uTexture)
{
	return Vsp::GraphicsSystem::Get().GetTextureBindlessSlot(uTexture);
}

// -------- Pipeline state builders --------

CSHARP_EXPORT uint32 VspRhi_CreatePipelineBuilder()
{
	return Vsp::GraphicsSystem::Get().CreatePipelineBuilder();
}

CSHARP_EXPORT void VspRhi_DestroyPipelineBuilder(uint32 uBuilder)
{
	Vsp::GraphicsSystem::Get().DestroyPipelineBuilder(uBuilder);
}

CSHARP_EXPORT void VspRhi_PipelineBuilderSetShader(uint32 uBuilder, int32 nStage, uint32 uShader)
{
	Vsp::GraphicsSystem::Get().PipelineBuilderSetShader(
		uBuilder, static_cast<Vsp::RhiShaderStage>(nStage), uShader);
}

CSHARP_EXPORT void VspRhi_PipelineBuilderSetVertexStride(uint32 uBuilder, uint32 uVertexStride)
{
	Vsp::GraphicsSystem::Get().PipelineBuilderSetVertexStride(uBuilder, uVertexStride);
}

CSHARP_EXPORT void VspRhi_PipelineBuilderAddVertexAttribute(
	uint32 uBuilder,
	int32 nShaderLocation,
	uint32 uComponentCount,
	uint32 uByteOffset)
{
	Vsp::GraphicsSystem::Get().PipelineBuilderAddVertexAttribute(
		uBuilder, nShaderLocation, uComponentCount, uByteOffset);
}

CSHARP_EXPORT void VspRhi_PipelineBuilderSetTopology(uint32 uBuilder, int32 nTopology)
{
	Vsp::GraphicsSystem::Get().PipelineBuilderSetTopology(
		uBuilder, static_cast<Vsp::RhiPrimitiveTopology>(nTopology));
}

CSHARP_EXPORT void VspRhi_PipelineBuilderSetCullMode(uint32 uBuilder, int32 nCullMode)
{
	Vsp::GraphicsSystem::Get().PipelineBuilderSetCullMode(
		uBuilder, static_cast<Vsp::RhiCullMode>(nCullMode));
}

CSHARP_EXPORT void VspRhi_PipelineBuilderSetDepthState(
	uint32 uBuilder,
	int32 bDepthTestEnabled,
	int32 bDepthWriteEnabled,
	int32 nDepthCompare)
{
	Vsp::GraphicsSystem::Get().PipelineBuilderSetDepthState(
		uBuilder,
		bDepthTestEnabled != 0,
		bDepthWriteEnabled != 0,
		static_cast<Vsp::RhiCompareOperation>(nDepthCompare));
}

CSHARP_EXPORT void VspRhi_PipelineBuilderSetBlendEnabled(uint32 uBuilder, int32 bBlendEnabled)
{
	Vsp::GraphicsSystem::Get().PipelineBuilderSetBlendEnabled(uBuilder, bBlendEnabled != 0);
}

CSHARP_EXPORT void VspRhi_PipelineBuilderSetPushConstantByteCount(uint32 uBuilder, uint32 uPushConstantByteCount)
{
	Vsp::GraphicsSystem::Get().PipelineBuilderSetPushConstantByteCount(uBuilder, uPushConstantByteCount);
}

CSHARP_EXPORT uint32 VspRhi_PipelineBuilderBuild(uint32 uBuilder)
{
	return Vsp::GraphicsSystem::Get().BuildPipelineFromBuilder(uBuilder);
}

CSHARP_EXPORT void VspRhi_DestroyPipeline(uint32 uPipeline)
{
	Vsp::GraphicsSystem::Get().DestroyGraphicsPipeline(uPipeline);
}

// -------- Frame recording (the command list of one frame) --------

CSHARP_EXPORT void VspRhi_BeginFrame()
{
	Vsp::RenderCore::Get().BeginFrame();
}

CSHARP_EXPORT void VspRhi_EndFrame()
{
	Vsp::RenderCore::Get().EndFrame();
}

CSHARP_EXPORT int32 VspRhi_IsFrameValid()
{
	return Vsp::RenderCore::Get().IsFrameValid() ? 1 : 0;
}

CSHARP_EXPORT void VspRhi_SetClearColor(float fColorR, float fColorG, float fColorB, float fColorA)
{
	Vsp::RenderCore::Get().SetClearColor(fColorR, fColorG, fColorB, fColorA);
}

// Hands the frame the camera the pipeline renders from. Everything the engine
// needs (the matrices a shader multiplies by and the lens the camera carries)
// travels in one call, so a pipeline sets its camera with one line.
CSHARP_EXPORT void VspRhi_SetFrameCamera(uint32 uCameraHandle)
{
	const Vsp::Camera* pCamera = Vsp::Scene::Get().FindCamera(uCameraHandle);
	if (pCamera == nullptr)
	{
		return;
	}

	Vsp::RhiFrameCamera frameCamera;
	pCamera->GetViewProjectionMatrix(frameCamera.m4ViewProjection);
	pCamera->GetViewMatrix(frameCamera.m4View);
	pCamera->GetProjectionMatrix(frameCamera.m4Projection);

	// The camera-to-world matrix is the view, inverted: the camera's own
	// transform.
	const Vsp::Transform* pTransform = Vsp::Scene::Get().FindTransform(pCamera->GetTransformHandle());
	if (pTransform != nullptr)
	{
		const_cast<Vsp::Transform*>(pTransform)->GetWorldMatrix(frameCamera.m4CameraToWorld);
	}

	pCamera->GetWorldPosition(
		frameCamera.fPosition[0], frameCamera.fPosition[1], frameCamera.fPosition[2]);

	frameCamera.fAperture = pCamera->GetAperture();
	frameCamera.fFocusDistance = pCamera->GetFocusDistance();
	frameCamera.fNearClipPlane = pCamera->GetNearClipPlane();
	frameCamera.fFarClipPlane = pCamera->GetFarClipPlane();
	frameCamera.bIsSet = true;

	Vsp::RenderCore::Get().SetFrameCamera(frameCamera);
}

CSHARP_EXPORT void VspRhi_CmdBeginRenderPass()
{
	Vsp::RenderCore::Get().BeginRenderPass();
}

CSHARP_EXPORT void VspRhi_CmdEndRenderPass()
{
	Vsp::RenderCore::Get().EndRenderPass();
}

CSHARP_EXPORT void VspRhi_CmdSetViewport(float fX, float fY, float fWidth, float fHeight)
{
	Vsp::RenderCore::Get().SetViewport(fX, fY, fWidth, fHeight);
}

CSHARP_EXPORT void VspRhi_CmdSetScissor(int32 nX, int32 nY, uint32 uWidth, uint32 uHeight)
{
	Vsp::RenderCore::Get().SetScissor(nX, nY, uWidth, uHeight);
}

CSHARP_EXPORT void VspRhi_CmdBindPipeline(uint32 uPipeline)
{
	Vsp::RenderCore::Get().BindPipeline(uPipeline);
}

CSHARP_EXPORT void VspRhi_CmdBindVertexBuffer(uint32 uVertexBuffer)
{
	Vsp::RenderCore::Get().BindVertexBuffer(uVertexBuffer);
}

CSHARP_EXPORT void VspRhi_CmdBindIndexBuffer(uint32 uIndexBuffer)
{
	Vsp::RenderCore::Get().BindIndexBuffer(uIndexBuffer);
}

CSHARP_EXPORT void VspRhi_CmdPushConstants(
	int32 nShaderStageFlags,
	uint32 uByteOffset,
	const void* pData,
	uint32 uByteCount)
{
	Vsp::RenderCore::Get().PushConstants(
		static_cast<uint32>(nShaderStageFlags), uByteOffset, pData, uByteCount);
}

CSHARP_EXPORT void VspRhi_CmdDraw(uint32 uVertexCount, uint32 uFirstVertex)
{
	Vsp::RenderCore::Get().Draw(uVertexCount, uFirstVertex);
}

CSHARP_EXPORT void VspRhi_CmdDrawIndexed(uint32 uIndexCount, uint32 uFirstIndex, uint32 uFirstVertex)
{
	Vsp::RenderCore::Get().DrawIndexed(uIndexCount, uFirstIndex, uFirstVertex);
}

// -------- Back buffer --------

CSHARP_EXPORT int32 VspRhi_GetBackbufferWidth()
{
	return static_cast<int32>(Vsp::GraphicsSystem::Get().GetBackbufferWidth());
}

CSHARP_EXPORT int32 VspRhi_GetBackbufferHeight()
{
	return static_cast<int32>(Vsp::GraphicsSystem::Get().GetBackbufferHeight());
}
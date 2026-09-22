#pragma once

#include "Core/Core.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Wrapped graphics API - shared types
	// -------------------------------------------------------------------------
	// The native layer exports the graphics API it already wrapped (device
	// resource creation plus a recorded command list). The managed render
	// pipeline is the only caller: it owns the flow of a frame and talks to
	// Vulkan exclusively through these types, so no managed code ever sees a
	// VkBuffer, VkPipeline or VkRenderPass.
	//
	// Every handle is an opaque 1-based index into the backend's resource
	// tables; 0 always means "invalid" and every create function returns 0 on
	// failure (the backend logs the reason through the engine Log module).
	// -------------------------------------------------------------------------

	using RhiBufferHandle = uint32;
	using RhiShaderHandle = uint32;
	using RhiTextureHandle = uint32;
	using RhiPipelineHandle = uint32;
	using RhiPipelineBuilderHandle = uint32;

	static constexpr uint32 k_nInvalidRhiHandle = 0;

	// Pipeline state builder: the caller assembles state piecewise and builds
	// one handle at the end.
	static constexpr uint32 k_nMaxPipelineBuilderCount = 16;
	static constexpr uint32 k_nMaxPipelineVertexAttributeCount = 8;

	// Push constants recorded per draw command. Vulkan guarantees at least 128
	// bytes, which is what a draw needs to carry its own world matrix; a fixed
	// payload keeps one recorded command a plain copyable value.
	static constexpr uint32 k_nMaxPushConstantByteCount = 128;
	static constexpr uint32 k_nMaxRecordedCommandCount = 4096;

	// Function name an entry point of a compiled shader may have. HLSL shaders
	// name their stages (PassVertex / PassFragment), so the name a shader was
	// compiled from travels with it and ends up in the pipeline stage.
	static constexpr uint32 k_nMaxShaderEntryPointNameLength = 64;

	enum class RhiShaderStage : int32
	{
		Vertex = 0,
		Fragment = 1,
	};

	enum class RhiPrimitiveTopology : int32
	{
		TriangleList = 0,
		TriangleStrip = 1,
		LineList = 2,
		PointList = 3,
	};

	// Which faces a rasterizer throws away. A closed 3D shape needs its back
	// faces culled; 2D content drawn on a plane usually turns culling off.
	enum class RhiCullMode : int32
	{
		None = 0,
		Front = 1,
		Back = 2,
	};

	// Depth comparison a pipeline applies when depth testing is on.
	enum class RhiCompareOperation : int32
	{
		Never = 0,
		Less = 1,
		Equal = 2,
		LessOrEqual = 3,
		Greater = 4,
		NotEqual = 5,
		GreaterOrEqual = 6,
		Always = 7,
	};

	enum class RhiCommandType : uint32
	{
		None = 0,
		BeginRenderPass = 1,
		EndRenderPass = 2,
		SetViewport = 3,
		SetScissor = 4,
		BindPipeline = 5,
		BindVertexBuffer = 6,
		PushConstants = 7,
		Draw = 8,
		BindIndexBuffer = 9,
		DrawIndexed = 10,
	};

	// -------------------------------------------------------------------------
	// RhiFrameCamera
	// -------------------------------------------------------------------------
	// The camera a frame is rendered from, as the render pipeline handed it over.
	// The backend writes it into the engine's camera uniform buffer (binding 0 of
	// the engine descriptor set), so every shader reads the camera the pipeline
	// chose without the pipeline having to manage a buffer.
	//
	// Every matrix is column-major, right-handed, with Vulkan clip space (X
	// right, Y down, depth 0..1) - what a Camera computes.
	// -------------------------------------------------------------------------
	struct RhiFrameCamera
	{
		float m4ViewProjection[16] = {};
		float m4View[16] = {};
		float m4Projection[16] = {};
		float m4CameraToWorld[16] = {};

		// World position of the camera (the last column of m4CameraToWorld, kept
		// separate because a shader reads it as a vector).
		float fPosition[3] = {};

		// Lens the camera was configured with: aperture as an f-number and the
		// distance it is focused at. 0 means "no depth of field information".
		float fAperture = 0.0f;
		float fFocusDistance = 0.0f;
		float fNearClipPlane = 0.0f;
		float fFarClipPlane = 0.0f;

		// True once a pipeline handed a camera over for this frame.
		bool bIsSet = false;
	};

	// One vertex input element of a graphics pipeline.
	struct RhiVertexAttribute
	{
		int32 nShaderLocation = 0;
		uint32 uComponentCount = 2;   // 1..4 floats.
		uint32 uByteOffset = 0;
	};

	// Buffer creation request.
	// What a buffer holds: the vertex data a draw reads, or the indices that
	// pick vertices out of it.
	enum class RhiBufferUsage : int32
	{
		Vertex = 0,
		Index = 1,
	};

	struct RhiBufferDescriptor
	{
		uint32 uByteSize = 0;
		RhiBufferUsage eUsage = RhiBufferUsage::Vertex;
		// Dynamic buffers stay host visible and are written directly;
		// non-dynamic buffers live in device-local memory and are filled
		// through a staging copy.
		bool bIsDynamic = false;
	};

	// Texture creation request (RGBA8 pixels).
	struct RhiTextureDescriptor
	{
		uint32 uWidth = 0;
		uint32 uHeight = 0;
	};

	// Everything needed to create one graphics pipeline. The render pass and
	// the descriptor set layout come from the backend (they belong to the
	// swapchain and to the bindless descriptor set), so a pipeline request only
	// describes the programmable and fixed-function state.
	struct RhiGraphicsPipelineState
	{
		RhiShaderHandle uVertexShader = k_nInvalidRhiHandle;
		RhiShaderHandle uFragmentShader = k_nInvalidRhiHandle;

		uint32 uVertexStride = 0;
		RhiVertexAttribute VertexAttributes[k_nMaxPipelineVertexAttributeCount];
		uint32 uVertexAttributeCount = 0;

		RhiPrimitiveTopology eTopology = RhiPrimitiveTopology::TriangleList;

		// Which faces to throw away before rasterizing.
		RhiCullMode eCullMode = RhiCullMode::None;

		// Depth test: off by default, because a pipeline that draws one flat
		// thing after another does not need it. A 3D scene turns it on and
		// writes depth so nearer surfaces hide farther ones.
		bool bDepthTestEnabled = false;
		bool bDepthWriteEnabled = false;
		RhiCompareOperation eDepthCompare = RhiCompareOperation::LessOrEqual;

		// Alpha blending (src alpha / one minus src alpha) - the 2D default.
		bool bBlendEnabled = true;

		// Size in bytes of the push-constant block every draw may set. The
		// render pipeline states it (the shader reflection reports it), so the
		// engine assumes no layout of its own.
		uint32 uPushConstantByteCount = 0;
	};

	// One recorded command of a frame. A tagged value with a fixed payload, so
	// recording never allocates and playback is a plain switch.
	struct RhiCommand
	{
		RhiCommandType eType = RhiCommandType::None;

		// Resource the command addresses (pipeline or buffer handle).
		uint32 uResourceHandle = 0;

		// Scalar operands; the meaning depends on eType:
		//   SetScissor      -> uValueA = x, uValueB = y, uValueC = width, uValueD = height
		//   Draw            -> uValueA = vertex count, uValueB = first vertex
		//   DrawIndexed     -> uValueA = index count, uValueB = first index, uValueC = first vertex
		//   PushConstants   -> uValueA = stage flags, uValueB = byte offset, uValueC = byte count
		uint32 uValueA = 0;
		uint32 uValueB = 0;
		uint32 uValueC = 0;
		uint32 uValueD = 0;

		// Floating operands; the meaning depends on eType:
		//   SetViewport     -> fValue[0..3] = x, y, width, height
		float fValue[4] = {};

		// Push-constant payload (PushConstants only).
		uint8 PushConstantBytes[k_nMaxPushConstantByteCount] = {};
	};
}

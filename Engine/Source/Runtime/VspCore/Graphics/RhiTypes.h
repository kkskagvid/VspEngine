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

	// Push constants recorded per draw command. The engine's shaders stay far
	// below this, and a fixed payload keeps one recorded command a plain
	// copyable value.
	static constexpr uint32 k_nMaxPushConstantByteCount = 64;
	static constexpr uint32 k_nMaxRecordedCommandCount = 4096;

	enum class RhiShaderStage : int32
	{
		Vertex = 0,
		Fragment = 1,
	};

	enum class RhiPrimitiveTopology : int32
	{
		TriangleList = 0,
		LineList = 1,
		PointList = 2,
	};

	// Which shader the engine's embedded SPIR-V belongs to.
	enum class RhiEmbeddedShader : int32
	{
		TriangleBindlessVertex = 0,
		TriangleBindlessFragment = 1,
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
	};

	// One vertex input element of a graphics pipeline.
	struct RhiVertexAttribute
	{
		int32 nShaderLocation = 0;
		uint32 uComponentCount = 2;   // 1..4 floats.
		uint32 uByteOffset = 0;
	};

	// Buffer creation request.
	struct RhiBufferDescriptor
	{
		uint32 uByteSize = 0;
		bool bIsVertexBuffer = true;
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

		// Alpha blending (src alpha / one minus src alpha) - the 2D default.
		bool bBlendEnabled = true;

		// Size of the push-constant block every draw may set.
		uint32 uPushConstantByteCount = 32;
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

using System;
using System.Runtime.InteropServices;

namespace VspEngine.Rendering
{
	/// <summary>The pipeline stage a shader is compiled for.</summary>
	public enum ShaderStage
	{
		Vertex = 0,
		Fragment = 1,
	}

	/// <summary>Primitive topologies the wrapped graphics API understands.</summary>
	public enum PrimitiveTopology
	{
		TriangleList = 0,
		LineList = 1,
		PointList = 2,
	}

	/// <summary>Shaders the engine ships as embedded SPIR-V.</summary>
	public enum EmbeddedShader
	{
		TriangleBindlessVertex = 0,
		TriangleBindlessFragment = 1,
	}

	/// <summary>
	/// Shader stages a recorded push-constant block addresses. The values are
	/// the native stage bits (VK_SHADER_STAGE_*), so they cross the interop
	/// boundary unchanged.
	/// </summary>
	[Flags]
	public enum ShaderStageFlags
	{
		None = 0,
		Vertex = 0x00000001,
		Fragment = 0x00000010,
	}

	/// <summary>
	/// Raw P/Invoke bindings of the native wrapped graphics API (VspRhi_* in
	/// VspCore). The managed render pipeline is the only caller: it creates
	/// device resources, assembles pipeline state and records the frame's
	/// command list. No managed code ever sees a Vulkan handle.
	///
	/// Every create function returns 0 on failure; the native side has already
	/// written the reason into the engine log.
	/// </summary>
	internal static class RhiApi
	{
		private const string LibraryName = "VspCore";

		// ---- Embedded shaders ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRhi_GetEmbeddedShaderByteCount(int embeddedShader);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRhi_GetEmbeddedShaderBytes(int embeddedShader, [Out] byte[] buffer, uint bufferCapacity);

		// ---- Buffers ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspRhi_CreateBuffer(uint byteSize, int isVertexBuffer, int isDynamic);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_DestroyBuffer(uint buffer);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRhi_UpdateBuffer(uint buffer, uint byteOffset, [In] byte[] data, uint byteCount);

		// ---- Shaders ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspRhi_CreateShader(int stage, [In] byte[] spirvCode, uint byteCount);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_DestroyShader(uint shader);

		// ---- Textures (bindless slots) ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspRhi_CreateTexture(uint width, uint height, [In] byte[] rgbaPixels);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_DestroyTexture(uint texture);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRhi_GetTextureBindlessSlot(uint texture);

		// ---- Pipeline state builders ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspRhi_CreatePipelineBuilder();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_DestroyPipelineBuilder(uint builder);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_PipelineBuilderSetShader(uint builder, int stage, uint shader);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_PipelineBuilderSetVertexStride(uint builder, uint vertexStride);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_PipelineBuilderAddVertexAttribute(uint builder, int shaderLocation, uint componentCount, uint byteOffset);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_PipelineBuilderSetTopology(uint builder, int topology);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_PipelineBuilderSetBlendEnabled(uint builder, int blendEnabled);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_PipelineBuilderSetPushConstantByteCount(uint builder, uint pushConstantByteCount);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspRhi_PipelineBuilderBuild(uint builder);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_DestroyPipeline(uint pipeline);

		// ---- Frame recording ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_BeginFrame();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_EndFrame();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRhi_IsFrameValid();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_SetClearColor(float red, float green, float blue, float alpha);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_CmdBeginRenderPass();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_CmdEndRenderPass();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_CmdSetViewport(float x, float y, float width, float height);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_CmdSetScissor(int x, int y, uint width, uint height);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_CmdBindPipeline(uint pipeline);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_CmdBindVertexBuffer(uint vertexBuffer);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_CmdPushConstants(int shaderStageFlags, uint byteOffset, [In] byte[] data, uint byteCount);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_CmdDraw(uint vertexCount, uint firstVertex);

		// ---- Back buffer ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRhi_GetBackbufferWidth();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRhi_GetBackbufferHeight();
	}
}

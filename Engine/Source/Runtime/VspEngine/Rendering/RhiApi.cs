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
		TriangleStrip = 1,
		LineList = 2,
		PointList = 3,
	}

	/// <summary>Which faces the rasterizer throws away.</summary>
	public enum CullMode
	{
		/// <summary>Draw both sides (what flat content usually wants).</summary>
		None = 0,

		/// <summary>Throw away front faces (looking at a shape from inside).</summary>
		Front = 1,

		/// <summary>Throw away back faces (what a closed 3D shape wants).</summary>
		Back = 2,
	}

	/// <summary>How a depth test compares a fragment with the depth buffer.</summary>
	public enum CompareOperation
	{
		Never = 0,
		Less = 1,
		Equal = 2,
		LessOrEqual = 3,
		Greater = 4,
		NotEqual = 5,
		GreaterOrEqual = 6,
		Always = 7,
	}

	/// <summary>What a buffer holds.</summary>
	public enum BufferUsage
	{
		Vertex = 0,
		Index = 1,
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

		// ---- Buffers ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspRhi_CreateBuffer(uint byteSize, int bufferUsage, int isDynamic);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_DestroyBuffer(uint buffer);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRhi_UpdateBuffer(uint buffer, uint byteOffset, [In] byte[] data, uint byteCount);

		// ---- Shaders ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspRhi_CreateShader(
			int stage,
			[MarshalAs(UnmanagedType.LPUTF8Str)] string entryPointName,
			[In] byte[] spirvCode,
			uint byteCount);

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
		internal static extern void VspRhi_PipelineBuilderSetCullMode(uint builder, int cullMode);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_PipelineBuilderSetDepthState(
			uint builder, int depthTestEnabled, int depthWriteEnabled, int depthCompare);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_PipelineBuilderSetBlendEnabled(uint builder, int blendEnabled);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_PipelineBuilderSetPushConstantByteCount(uint builder, uint pushConstantByteCount);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspRhi_PipelineBuilderBuild(uint builder);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_DestroyPipeline(uint pipeline);

		// ---- Frame recording ----
		// The frame is opened by the engine's rendering system before the scripts
		// run; a pipeline records into it and closes it through Submit().
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_EndFrame();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRhi_IsFrameValid();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_SetClearColor(float red, float green, float blue, float alpha);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_SetFrameCamera(uint camera);

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
		internal static extern void VspRhi_CmdBindIndexBuffer(uint indexBuffer);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_CmdPushConstants(int shaderStageFlags, uint byteOffset, [In] byte[] data, uint byteCount);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_CmdDraw(uint vertexCount, uint firstVertex);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRhi_CmdDrawIndexed(uint indexCount, uint firstIndex, uint firstVertex);

		// ---- Back buffer ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRhi_GetBackbufferWidth();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRhi_GetBackbufferHeight();
	}
}
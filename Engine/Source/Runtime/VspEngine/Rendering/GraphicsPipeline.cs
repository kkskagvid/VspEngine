using System;

namespace VspEngine.Rendering
{
	/// <summary>One vertex input element of a graphics pipeline.</summary>
	public readonly struct VertexAttribute
	{
		/// <summary>Location the shader declares the element at.</summary>
		public readonly int ShaderLocation;

		/// <summary>Number of float components (1..4).</summary>
		public readonly uint ComponentCount;

		/// <summary>Byte offset of the element inside one vertex.</summary>
		public readonly uint ByteOffset;

		public VertexAttribute(int shaderLocation, uint componentCount, uint byteOffset)
		{
			ShaderLocation = shaderLocation;
			ComponentCount = componentCount;
			ByteOffset = byteOffset;
		}
	}

	/// <summary>
	/// A graphics pipeline: the shaders plus the vertex, topology and blend state
	/// the draws recorded with it use. Pipelines are created through
	/// <see cref="GraphicsPipelineBuilder"/>, which keeps the state assembly
	/// explicit and lets it cross the interop boundary one field at a time.
	/// </summary>
	public sealed class GraphicsPipeline : IDisposable
	{
		private uint nativeHandle;

		internal GraphicsPipeline(uint handle)
		{
			nativeHandle = handle;
		}

		/// <summary>Native pipeline handle (0 = invalid).</summary>
		public uint NativeHandle => nativeHandle;

		public bool IsValid => nativeHandle != 0;

		public void Dispose()
		{
			if (nativeHandle != 0)
			{
				RhiApi.VspRhi_DestroyPipeline(nativeHandle);
				nativeHandle = 0;
			}
		}
	}

	/// <summary>
	/// Assembles the state of one graphics pipeline and builds it. The native
	/// side owns the parts a pipeline cannot choose (the swapchain render pass
	/// and the bindless descriptor set layout) and combines them with the state
	/// assembled here.
	/// </summary>
	public sealed class GraphicsPipelineBuilder : IDisposable
	{
		private uint nativeHandle;

		public GraphicsPipelineBuilder()
		{
			nativeHandle = RhiApi.VspRhi_CreatePipelineBuilder();
		}

		public bool IsValid => nativeHandle != 0;

		/// <summary>Sets the shader module of one pipeline stage.</summary>
		public GraphicsPipelineBuilder SetShader(ShaderStage stage, ShaderModule shaderModule)
		{
			if (shaderModule == null)
			{
				throw new ArgumentNullException(nameof(shaderModule));
			}

			RhiApi.VspRhi_PipelineBuilderSetShader(nativeHandle, (int)stage, shaderModule.NativeHandle);
			return this;
		}

		/// <summary>Sets the byte size of one vertex, which every attribute offset is relative to.</summary>
		public GraphicsPipelineBuilder SetVertexStride(uint vertexStride)
		{
			RhiApi.VspRhi_PipelineBuilderSetVertexStride(nativeHandle, vertexStride);
			return this;
		}

		/// <summary>Declares one vertex input element.</summary>
		public GraphicsPipelineBuilder AddVertexAttribute(VertexAttribute attribute)
		{
			RhiApi.VspRhi_PipelineBuilderAddVertexAttribute(
				nativeHandle, attribute.ShaderLocation, attribute.ComponentCount, attribute.ByteOffset);
			return this;
		}

		/// <summary>Declares the whole vertex layout in one call.</summary>
		public GraphicsPipelineBuilder SetVertexLayout(uint vertexStride, params VertexAttribute[] attributes)
		{
			SetVertexStride(vertexStride);
			if (attributes != null)
			{
				foreach (VertexAttribute attribute in attributes)
				{
					AddVertexAttribute(attribute);
				}
			}
			return this;
		}

		public GraphicsPipelineBuilder SetTopology(PrimitiveTopology topology)
		{
			RhiApi.VspRhi_PipelineBuilderSetTopology(nativeHandle, (int)topology);
			return this;
		}

		public GraphicsPipelineBuilder SetBlendEnabled(bool blendEnabled)
		{
			RhiApi.VspRhi_PipelineBuilderSetBlendEnabled(nativeHandle, blendEnabled ? 1 : 0);
			return this;
		}

		/// <summary>Which faces the rasterizer throws away.</summary>
		public GraphicsPipelineBuilder SetCullMode(CullMode cullMode)
		{
			RhiApi.VspRhi_PipelineBuilderSetCullMode(nativeHandle, (int)cullMode);
			return this;
		}

		/// <summary>
		/// Turns the depth test on or off. A 3D pipeline turns it on and writes
		/// depth, so a nearer surface hides a farther one; a pipeline that draws
		/// one flat thing (2D content) leaves it off.
		/// </summary>
		public GraphicsPipelineBuilder SetDepthTest(
			bool depthTestEnabled,
			bool depthWriteEnabled = true,
			CompareOperation depthCompare = CompareOperation.LessOrEqual)
		{
			RhiApi.VspRhi_PipelineBuilderSetDepthState(
				nativeHandle, depthTestEnabled ? 1 : 0, depthWriteEnabled ? 1 : 0, (int)depthCompare);
			return this;
		}

		/// <summary>Size in bytes of the push-constant block draws may set.</summary>
		public GraphicsPipelineBuilder SetPushConstantByteCount(uint pushConstantByteCount)
		{
			RhiApi.VspRhi_PipelineBuilderSetPushConstantByteCount(nativeHandle, pushConstantByteCount);
			return this;
		}

		/// <summary>
		/// Creates the pipeline from the assembled state and releases the
		/// builder. Returns null when the backend refused the state (the reason
		/// is in the engine log).
		/// </summary>
		public GraphicsPipeline? Build()
		{
			if (nativeHandle == 0)
			{
				return null;
			}

			uint pipelineHandle = RhiApi.VspRhi_PipelineBuilderBuild(nativeHandle);
			nativeHandle = 0;   // The backend releases the builder either way.
			return pipelineHandle != 0 ? new GraphicsPipeline(pipelineHandle) : null;
		}

		public void Dispose()
		{
			if (nativeHandle != 0)
			{
				RhiApi.VspRhi_DestroyPipelineBuilder(nativeHandle);
				nativeHandle = 0;
			}
		}
	}
}

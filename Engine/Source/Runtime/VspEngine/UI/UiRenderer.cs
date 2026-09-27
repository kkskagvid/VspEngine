using System;
using System.Collections.Generic;
using System.Numerics;
using System.Runtime.InteropServices;
using VspEngine.Rendering;

namespace VspEngine.UI
{
	/// <summary>
	/// The push-constant block the UI shader declares. The layout and the explicit
	/// offsets must stay identical to the UiPushConstants block in
	/// Assembly/Shaders/UiQuadCommon.hlsl - the reflection HLSLCC produced for
	/// that block is what the renderer checks itself against.
	/// </summary>
	[StructLayout(LayoutKind.Sequential, Pack = 4)]
	internal struct UiPushConstants
	{
		public Vector4 TintColor;      // offset 0
		public uint TextureIndex;      // offset 16  (bindless slot this draw samples)
		public float Padding0;         // offset 20
		public float Padding1;         // offset 24
		public float Padding2;         // offset 28
	}

	/// <summary>
	/// Draws a <see cref="Canvas"/> through the wrapped graphics API.
	///
	/// The renderer owns everything the interface needs on the device: the UI
	/// shader, one graphics pipeline, a dynamic vertex and index buffer that the
	/// whole frame is written into, and the 1x1 white texture solid fills sample.
	/// Fonts are not owned here - they belong to the canvas - but their coverage
	/// atlases are uploaded from here, because that is where the textures live.
	///
	/// A frame is three steps, and the middle one can repeat once:
	///
	///   1. upload every font atlas that grew,
	///   2. build the draw list (which may rasterize new glyphs into an atlas),
	///   3. when step 2 changed an atlas, upload again and rebuild the list - so
	///      a glyph that was just rasterized is on screen in the SAME frame
	///      instead of the next one.
	///
	/// Every failure is reported through <see cref="Debug"/> and turns into "this
	/// frame draws no interface"; nothing in the UI path takes the frame down.
	/// </summary>
	public sealed class UiRenderer : IDisposable
	{
		/// <summary>Name of the compiled shader the interface draws with.</summary>
		public const string ShaderName = "UiQuad";

		// One frame's worth of interface geometry. 4096 quads is far more than a
		// flat interface uses (a screen full of panels and labels is a few
		// hundred), and the buffers are allocated once and rewritten per frame.
		private const int MaxQuadCount = 4096;
		private const int MaxVertexCount = MaxQuadCount * 4;
		private const int MaxIndexCount = MaxQuadCount * 6;

		private static readonly ShaderStageFlags PushConstantStages = ShaderStageFlags.Vertex | ShaderStageFlags.Fragment;

		private Shader? shader;
		private GraphicsPipeline? pipeline;
		private VertexBuffer? vertexBuffer;
		private IndexBuffer? indexBuffer;
		private Texture2D? solidTexture;
		private UiVertex[] vertexScratch = new UiVertex[MaxVertexCount];
		private uint[] indexScratch = new uint[MaxIndexCount];

		// The one-canvas entry point is built on the multi-canvas one, so both go
		// through exactly the same upload and recording path.
		private static readonly List<Canvas> singleCanvasScratch = new List<Canvas>(1);

		private bool initializationFailed;

		/// <summary>True once the shader, the pipeline and the buffers exist.</summary>
		public bool IsReady { get; private set; }

		/// <summary>The shader the interface draws with, once it was loaded.</summary>
		public Shader? Shader => shader;

		/// <summary>
		/// Bindless slot of the 1x1 white texture solid fills sample (-1 before
		/// the renderer is ready). The canvas publishes it to the widgets.
		/// </summary>
		public int SolidTextureBindlessSlot => solidTexture?.BindlessSlot ?? -1;

		/// <summary>
		/// Loads the UI shader and creates the device resources, once. Returns
		/// false when the interface cannot be drawn, which a pipeline reports and
		/// then lives with - the game keeps rendering without a UI.
		/// </summary>
		public bool EnsureReady()
		{
			if (IsReady)
			{
				return true;
			}
			if (initializationFailed)
			{
				return false;
			}

			shader = Shader.Load(ShaderName);
			if (shader == null || !shader.IsValid)
			{
				return FailInitialization("the compiled shader '" + ShaderName + "' could not be loaded");
			}

			solidTexture = Texture2D.CreateWhite(1, 1);
			if (solidTexture == null || !solidTexture.IsValid)
			{
				return FailInitialization("the solid-fill texture could not be created");
			}

			vertexBuffer = new VertexBuffer((uint)(MaxVertexCount * UiVertex.Stride), isDynamic: true);
			indexBuffer = new IndexBuffer(MaxIndexCount, isDynamic: true);
			if (!vertexBuffer.IsValid || !indexBuffer.IsValid)
			{
				return FailInitialization("the UI vertex or index buffer could not be allocated");
			}

			pipeline = CreatePipeline();
			if (pipeline == null || !pipeline.IsValid)
			{
				return FailInitialization("the UI graphics pipeline could not be created");
			}

			IsReady = true;
			Debug.LogInfo("UiRenderer: shader '" + shader.ShaderName + "' ready (" + shader.VariantCount + " variant(s)).");
			return true;
		}

		/// <summary>
		/// Builds the frame's interface and records its draws into the command
		/// buffer. The caller has already opened the render pass; the UI is drawn
		/// after the 3D scene, depth-tested against it and never writing depth.
		/// </summary>
		public bool Render(CommandBuffer commandBuffer, Canvas canvas, int width, int height)
		{
			if (canvas == null)
			{
				return false;
			}

			singleCanvasScratch.Clear();
			singleCanvasScratch.Add(canvas);
			return Render(commandBuffer, singleCanvasScratch, width, height);
		}

		/// <summary>
		/// Builds the frame's interface out of EVERY canvas of the frame - the
		/// JSON-described layout first, the code-built (immediate) one after it -
		/// and records its draws into the command buffer.
		///
		/// All canvases go into ONE draw list and ONE buffer upload, because the
		/// vertex and index buffers are per frame rather than per canvas: a second
		/// upload would overwrite the geometry the first canvas just recorded.
		/// Recording order is therefore drawing order, and the immediate interface
		/// is on top of the layout one.
		///
		/// The caller has already opened the render pass; the UI is drawn after the
		/// 3D scene, depth-tested against it and never writing depth.
		/// </summary>
		public bool Render(CommandBuffer commandBuffer, IReadOnlyList<Canvas> canvases, int width, int height)
		{
			if (commandBuffer == null || canvases == null || canvases.Count == 0)
			{
				return false;
			}
			if (!EnsureReady())
			{
				return false;
			}
			if (width <= 0 || height <= 0)
			{
				return false;
			}

			// 1. Upload whatever is already rasterized, and tell every canvas where
			//    the solid-fill texture lives.
			for (int canvasIndex = 0; canvasIndex < canvases.Count; ++canvasIndex)
			{
				Canvas? canvas = canvases[canvasIndex];
				if (canvas == null)
				{
					continue;
				}

				canvas.SolidTextureBindlessSlot = solidTexture!.BindlessSlot;
				canvas.SyncFontAtlases();
			}

			// 2. Build the list; this is what rasterizes glyphs on first use.
			UiDrawList drawList = new UiDrawList();
			BuildDrawList(drawList, canvases, width, height);

			// 3. A glyph rasterized in step 2 changed an atlas; upload it and
			//    build the list again so the new glyphs are drawn this frame.
			bool didAnyAtlasChange = false;
			for (int canvasIndex = 0; canvasIndex < canvases.Count; ++canvasIndex)
			{
				Canvas? canvas = canvases[canvasIndex];
				if (canvas != null && canvas.SyncFontAtlases())
				{
					didAnyAtlasChange = true;
				}
			}

			if (didAnyAtlasChange)
			{
				drawList.Clear();
				BuildDrawList(drawList, canvases, width, height);
			}

			if (drawList.Commands.Count == 0)
			{
				return true;
			}

			return RecordDraws(commandBuffer, drawList, width, height);
		}

		/// <summary>Appends every canvas of the frame to one draw list, in order.</summary>
		private static void BuildDrawList(UiDrawList drawList, IReadOnlyList<Canvas> canvases, int width, int height)
		{
			for (int canvasIndex = 0; canvasIndex < canvases.Count; ++canvasIndex)
			{
				Canvas? canvas = canvases[canvasIndex];
				if (canvas != null && canvas.IsVisible)
				{
					canvas.BuildDrawList(drawList, width, height);
				}
			}
		}

		public void Dispose()
		{
			pipeline?.Dispose();
			pipeline = null;
			vertexBuffer?.Dispose();
			vertexBuffer = null;
			indexBuffer?.Dispose();
			indexBuffer = null;
			solidTexture?.Dispose();
			solidTexture = null;
			shader = null;

			IsReady = false;
			initializationFailed = false;
		}

		// -----------------------------------------------------------------
		// Recording
		// -----------------------------------------------------------------

		private bool RecordDraws(CommandBuffer commandBuffer, UiDrawList drawList, int width, int height)
		{
			int vertexCount = drawList.Vertices.Count;
			int indexCount = drawList.Indices.Count;
			if (vertexCount == 0 || indexCount == 0)
			{
				return true;
			}

			if (vertexCount > vertexScratch.Length || indexCount > indexScratch.Length)
			{
				// The interface outgrew the buffers: report it once and draw the
				// part that fits rather than corrupting the buffers.
				Debug.LogError("UiRenderer: the interface needs " + (vertexCount / 4)
					+ " quad(s), more than the " + MaxQuadCount + " the buffers hold; the excess is dropped.");
				vertexCount = Math.Min(vertexCount, vertexScratch.Length);
				indexCount = Math.Min(indexCount, indexScratch.Length);
			}

			for (int vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
			{
				vertexScratch[vertexIndex] = drawList.Vertices[vertexIndex];
			}
			for (int index = 0; index < indexCount; ++index)
			{
				indexScratch[index] = drawList.Indices[index];
			}

			if (!vertexBuffer!.Update(new ReadOnlySpan<UiVertex>(vertexScratch, 0, vertexCount)))
			{
				Debug.LogError("UiRenderer: the UI vertex buffer could not be filled.");
				return false;
			}
			if (!indexBuffer!.Update(new ReadOnlySpan<uint>(indexScratch, 0, indexCount)))
			{
				Debug.LogError("UiRenderer: the UI index buffer could not be filled.");
				return false;
			}

			commandBuffer.SetViewport(0.0f, 0.0f, width, height);
			commandBuffer.SetScissor(0, 0, (uint)width, (uint)height);
			commandBuffer.BindPipeline(pipeline!);
			commandBuffer.BindVertexBuffer(vertexBuffer!);
			commandBuffer.BindIndexBuffer(indexBuffer!);

			for (int commandIndex = 0; commandIndex < drawList.Commands.Count; ++commandIndex)
			{
				UiDrawCommand command = drawList.Commands[commandIndex];

				// The draw stays inside what was uploaded, so a truncated frame
				// cannot read past the buffers.
				int firstIndex = Math.Min(command.FirstIndex, indexCount);
				int count = Math.Min(command.IndexCount, indexCount - firstIndex);
				if (count <= 0 || command.TextureBindlessSlot < 0)
				{
					continue;
				}

				UiPushConstants pushConstants = default;
				pushConstants.TintColor = Vector4.One;
				pushConstants.TextureIndex = (uint)command.TextureBindlessSlot;

				commandBuffer.PushConstants(PushConstantStages, in pushConstants);
				commandBuffer.DrawIndexed((uint)count, (uint)firstIndex);
			}

			return true;
		}

		/// <summary>
		/// One pipeline for the whole interface: two triangles per quad, no
		/// culling (a mirrored layout still has to draw), depth-tested but never
		/// depth-writing, alpha-blended so glyph coverage reads as an edge.
		/// </summary>
		private GraphicsPipeline? CreatePipeline()
		{
			if (shader == null)
			{
				return null;
			}

			ShaderModule? vertexShader = CreateStageShader(ShaderStage.Vertex);
			ShaderModule? fragmentShader = CreateStageShader(ShaderStage.Fragment);
			if (vertexShader == null || fragmentShader == null)
			{
				vertexShader?.Dispose();
				fragmentShader?.Dispose();
				return null;
			}

			ShaderStageReflectionSummary reflection = shader.GetStageReflection(0, ShaderStage.Vertex);
			int pushConstantByteCount = (int)reflection.PushConstantByteSize;
			if (pushConstantByteCount <= 0)
			{
				pushConstantByteCount = Marshal.SizeOf<UiPushConstants>();
			}

			GraphicsPipeline? createdPipeline;
			using (GraphicsPipelineBuilder builder = new GraphicsPipelineBuilder())
			{
				if (!builder.IsValid)
				{
					vertexShader.Dispose();
					fragmentShader.Dispose();
					return null;
				}

				builder
					.SetShader(ShaderStage.Vertex, vertexShader)
					.SetShader(ShaderStage.Fragment, fragmentShader)
					.SetVertexLayout(
						UiVertex.Stride,
						new VertexAttribute(0, 2, 0),     // clip-space position
						new VertexAttribute(1, 2, 8),     // texture coordinate
						new VertexAttribute(2, 4, 16))    // colour
					.SetTopology(PrimitiveTopology.TriangleList)
					.SetCullMode(CullMode.None)
					.SetDepthTest(true, false, CompareOperation.LessOrEqual)
					.SetBlendEnabled(true)
					.SetPushConstantByteCount((uint)pushConstantByteCount);

				createdPipeline = builder.Build();
			}

			vertexShader.Dispose();
			fragmentShader.Dispose();

			if (createdPipeline == null || !createdPipeline.IsValid)
			{
				Debug.LogError("UiRenderer: the UI pipeline could not be created.");
				return null;
			}
			return createdPipeline;
		}

		private ShaderModule? CreateStageShader(ShaderStage stage)
		{
			int byteCount = shader!.GetStageSpirvSize(0, stage);
			if (byteCount <= 0)
			{
				Debug.LogError("UiRenderer: the shader has no " + stage + " module.");
				return null;
			}

			byte[] spirvCode = new byte[byteCount];
			if (!shader.CopyStageSpirv(0, stage, spirvCode))
			{
				Debug.LogError("UiRenderer: the " + stage + " module could not be read.");
				return null;
			}

			ShaderModule compiledStageShader = new ShaderModule(stage, shader.GetEntryPointName(0, stage), spirvCode);
			if (!compiledStageShader.IsValid)
			{
				Debug.LogError("UiRenderer: the backend rejected the " + stage + " module.");
				return null;
			}
			return compiledStageShader;
		}

		private bool FailInitialization(string reason)
		{
			if (!initializationFailed)
			{
				initializationFailed = true;
				Debug.LogError("UiRenderer: " + reason + "; the interface is not drawn.");
			}
			return false;
		}
	}
}

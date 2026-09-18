using System.Numerics;
using System.Runtime.InteropServices;

namespace VspEngine.Rendering.Pipelines
{
	/// <summary>
	/// The push-constant block the engine's 2D shaders declare: a per-draw
	/// position offset (vertex stage) plus the color override, color mode and
	/// bindless texture slot (fragment stage). The layout must stay identical to
	/// the GLSL blocks in TriangleBindless.vert / TriangleBindless.frag.
	/// </summary>
	[StructLayout(LayoutKind.Sequential, Pack = 4)]
	internal struct TrianglePushConstants
	{
		public float PositionOffsetX;
		public float PositionOffsetY;
		public float OverrideColorR;
		public float OverrideColorG;
		public float OverrideColorB;
		public float OverrideColorA;
		public int ColorMode;
		public uint TextureIndex;
	}

	/// <summary>
	/// The engine's default render pipeline: one forward pass that clears the
	/// background and draws every drawable the manager gathered as a colored
	/// triangle, using the built-in bindless 2D shaders.
	///
	/// It is a normal <see cref="RenderPipeline"/>, so a game assembly can study
	/// it and replace it with its own flow; nothing about it is privileged.
	/// </summary>
	public sealed class Forward2DRenderPipeline : RenderPipeline
	{
		/// <summary>Background the frame clears to.</summary>
		public static readonly Color BackgroundColor = new Color(0.06f, 0.06f, 0.10f, 1.0f);

		private const int TriangleVertexCount = 3;
		private const uint DefaultTextureSize = 8;

		// The acceptance triangle: one vertex per corner, distinct vertex colors.
		private static readonly Vertex2D[] TriangleVertices =
		{
			// bottom-left (red)    bottom-right (green)  top (blue)
			new Vertex2D(new Vector2(-0.5f, -0.45f), new Vector4(1.0f, 0.0f, 0.0f, 1.0f), new Vector2(0.0f, 0.0f)),
			new Vertex2D(new Vector2( 0.5f, -0.45f), new Vector4(0.0f, 1.0f, 0.0f, 1.0f), new Vector2(1.0f, 0.0f)),
			new Vertex2D(new Vector2( 0.0f,  0.55f), new Vector4(0.0f, 0.0f, 1.0f, 1.0f), new Vector2(0.5f, 1.0f)),
		};

		private Shader? vertexShader;
		private Shader? fragmentShader;
		private VertexBuffer? triangleVertexBuffer;
		private Texture2D? defaultTexture;
		private GraphicsPipeline? trianglePipeline;
		private bool resourcesReady;
		private bool resourceCreationFailed;

		public override void Render(ScriptableRenderContext context)
		{
			EnsureResources();

			CommandBuffer commandBuffer = context.CommandBuffer;

			if (!resourcesReady)
			{
				// Without a pipeline the frame still clears, so the window keeps
				// showing the background instead of stale pixels.
				commandBuffer.BeginRenderPass(BackgroundColor);
				commandBuffer.EndRenderPass();
				context.Submit();
				return;
			}

			commandBuffer.BeginRenderPass(BackgroundColor);
			commandBuffer.SetViewport(0.0f, 0.0f, context.BackbufferWidth, context.BackbufferHeight);
			commandBuffer.SetScissor(0, 0, (uint)context.BackbufferWidth, (uint)context.BackbufferHeight);
			commandBuffer.BindPipeline(trianglePipeline!);
			commandBuffer.BindVertexBuffer(triangleVertexBuffer!);

			// Vertex colors carry the multicolor mode; every other mode is an
			// override the fragment stage applies.
			ShaderStageFlags pushConstantStages = ShaderStageFlags.Vertex | ShaderStageFlags.Fragment;
			uint textureIndex = (uint)defaultTexture!.BindlessSlot;

			foreach (RenderDrawItem drawItem in context.DrawItems)
			{
				TrianglePushConstants pushConstants = BuildPushConstants(drawItem, textureIndex);
				commandBuffer.PushConstants(pushConstantStages, in pushConstants);
				commandBuffer.Draw(TriangleVertexCount);
			}

			commandBuffer.EndRenderPass();
			context.Submit();
		}

		public override void Dispose()
		{
			trianglePipeline?.Dispose();
			trianglePipeline = null;

			triangleVertexBuffer?.Dispose();
			triangleVertexBuffer = null;

			defaultTexture?.Dispose();
			defaultTexture = null;

			vertexShader?.Dispose();
			vertexShader = null;

			fragmentShader?.Dispose();
			fragmentShader = null;

			resourcesReady = false;
			resourceCreationFailed = false;
		}

		/// <summary>
		/// Creates the pipeline and its resources on first use, which is also the
		/// first frame after the graphics backend came up.
		/// </summary>
		private void EnsureResources()
		{
			if (resourcesReady || resourceCreationFailed)
			{
				return;
			}

			vertexShader = Shader.CreateFromEmbedded(EmbeddedShader.TriangleBindlessVertex, ShaderStage.Vertex);
			fragmentShader = Shader.CreateFromEmbedded(EmbeddedShader.TriangleBindlessFragment, ShaderStage.Fragment);
			if (vertexShader == null || fragmentShader == null)
			{
				FailResourceCreation("the built-in shaders could not be created");
				return;
			}

			triangleVertexBuffer = new VertexBuffer((uint)(TriangleVertices.Length * Vertex2D.Stride));
			if (!triangleVertexBuffer.IsValid || !triangleVertexBuffer.Update<Vertex2D>(TriangleVertices))
			{
				FailResourceCreation("the triangle vertex buffer could not be filled");
				return;
			}

			// The white texture keeps the acceptance colors exact while the
			// bindless array sampling is exercised.
			defaultTexture = Texture2D.CreateWhite(DefaultTextureSize, DefaultTextureSize);
			if (defaultTexture == null || !defaultTexture.IsValid)
			{
				FailResourceCreation("the default white texture could not be created");
				return;
			}

			using (GraphicsPipelineBuilder builder = new GraphicsPipelineBuilder())
			{
				if (!builder.IsValid)
				{
					FailResourceCreation("the pipeline builder could not be created");
					return;
				}

				builder
					.SetShader(ShaderStage.Vertex, vertexShader)
					.SetShader(ShaderStage.Fragment, fragmentShader)
					.SetVertexLayout(
						Vertex2D.Stride,
						new VertexAttribute(0, 2, 0),    // position
						new VertexAttribute(1, 4, 8),    // color
						new VertexAttribute(2, 2, 24))   // uv
					.SetTopology(PrimitiveTopology.TriangleList)
					.SetBlendEnabled(true)
					.SetPushConstantByteCount((uint)Marshal.SizeOf<TrianglePushConstants>());

				trianglePipeline = builder.Build();
			}

			if (trianglePipeline == null || !trianglePipeline.IsValid)
			{
				FailResourceCreation("the triangle pipeline could not be created");
				return;
			}

			resourcesReady = true;
			Debug.LogInfo("Forward2DRenderPipeline: pipeline ready (bindless 2D, texture slot "
				+ defaultTexture.BindlessSlot + ").");
		}

		/// <summary>
		/// Reports a one-time resource failure and leaves the pipeline in the
		/// clear-only state instead of retrying every frame.
		/// </summary>
		private void FailResourceCreation(string reason)
		{
			if (!resourceCreationFailed)
			{
				resourceCreationFailed = true;
				Debug.LogError("Forward2DRenderPipeline: " + reason + "; frames fall back to a plain clear.");
			}
		}

		/// <summary>
		/// Translates a draw item into the shader constants: where the triangle
		/// sits and which color it shows.
		/// </summary>
		private static TrianglePushConstants BuildPushConstants(RenderDrawItem drawItem, uint textureIndex)
		{
			TrianglePushConstants pushConstants = default;
			pushConstants.PositionOffsetX = drawItem.Position.X;
			pushConstants.PositionOffsetY = drawItem.Position.Y;
			pushConstants.ColorMode = (int)drawItem.ColorMode;
			pushConstants.TextureIndex = textureIndex;

			// MultiColor keeps the vertex colors, so its override stays zero and
			// the fragment stage ignores it (uColorMode == 3).
			switch (drawItem.ColorMode)
			{
			case ColorMode.Red:
				pushConstants.OverrideColorR = 1.0f;
				pushConstants.OverrideColorA = 1.0f;
				break;

			case ColorMode.Blue:
				pushConstants.OverrideColorB = 1.0f;
				pushConstants.OverrideColorA = 1.0f;
				break;

			case ColorMode.Green:
				pushConstants.OverrideColorG = 1.0f;
				pushConstants.OverrideColorA = 1.0f;
				break;

			default:
				break;
			}

			return pushConstants;
		}
	}
}

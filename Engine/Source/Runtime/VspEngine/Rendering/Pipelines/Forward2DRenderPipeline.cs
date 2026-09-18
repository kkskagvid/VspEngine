using System;
using System.Collections.Generic;
using System.Numerics;
using System.Runtime.InteropServices;

namespace VspEngine.Rendering.Pipelines
{
	/// <summary>
	/// The push-constant block the engine's 2D shader declares: the color
	/// override, the material tint, the per-draw position offset, the color mode
	/// and the bindless texture slot. The field order and the explicit offsets
	/// must stay identical to the PassPushConstants block in
	/// Graphics/Shaders/Triangle2DCommon.hlsl - the reflection HLSLCC produces for
	/// that block is what the pipeline checks itself against.
	/// </summary>
	[StructLayout(LayoutKind.Sequential, Pack = 4)]
	internal struct TrianglePushConstants
	{
		public float OverrideColorR;   // offset 0
		public float OverrideColorG;
		public float OverrideColorB;
		public float OverrideColorA;
		public float TintColorR;       // offset 16
		public float TintColorG;
		public float TintColorB;
		public float TintColorA;
		public float PositionOffsetX;  // offset 32
		public float PositionOffsetY;
		public int ColorMode;          // offset 40
		public uint TextureIndex;      // offset 44
	}

	/// <summary>
	/// The engine's default render pipeline: one forward pass that clears the
	/// background and draws every drawable the manager gathered as a colored
	/// triangle.
	///
	/// The shader is NOT written here: it is the SPIR-V HLSLCC produced from
	/// Shaders/Triangle2D.vsf, which the engine loads from disk. The pipeline
	/// builds one graphics pipeline per shader VARIANT and picks between them
	/// from the material each drawable carries - the same way a game pipeline
	/// would.
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

		/// <summary>Base name of the compiled shader this pipeline draws with.</summary>
		public const string DefaultShaderName = "Triangle2D";

		// The acceptance triangle: one vertex per corner, distinct vertex colors.
		private static readonly Vertex2D[] TriangleVertices =
		{
			// bottom-left (red)    bottom-right (green)  top (blue)
			new Vertex2D(new Vector2(-0.5f, -0.45f), new Vector4(1.0f, 0.0f, 0.0f, 1.0f), new Vector2(0.0f, 0.0f)),
			new Vertex2D(new Vector2( 0.5f, -0.45f), new Vector4(0.0f, 1.0f, 0.0f, 1.0f), new Vector2(1.0f, 0.0f)),
			new Vertex2D(new Vector2( 0.0f,  0.55f), new Vector4(0.0f, 0.0f, 1.0f, 1.0f), new Vector2(0.5f, 1.0f)),
		};

		private Shader? shader;
		private Material? defaultMaterial;
		private VertexBuffer? triangleVertexBuffer;
		private Texture2D? defaultTexture;

		// One graphics pipeline per shader variant, built on first use.
		private readonly Dictionary<int, GraphicsPipeline> pipelinesByVariant = new Dictionary<int, GraphicsPipeline>();

		private bool resourcesReady;
		private bool resourceCreationFailed;

		/// <summary>The shader the pipeline loaded, once it has one.</summary>
		public Shader? Shader => shader;

		/// <summary>The material given to drawables that do not carry one.</summary>
		public Material? DefaultMaterial => defaultMaterial;

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
			commandBuffer.BindVertexBuffer(triangleVertexBuffer!);

			// Vertex colors carry the multicolor mode; every other mode is an
			// override the fragment stage applies.
			ShaderStageFlags pushConstantStages = ShaderStageFlags.Vertex | ShaderStageFlags.Fragment;
			uint textureIndex = (uint)defaultTexture!.BindlessSlot;

			foreach (RenderDrawItem drawItem in context.DrawItems)
			{
				Material material = ResolveMaterial(drawItem);
				GraphicsPipeline? pipeline = GetOrCreatePipeline(material.ResolveVariantIndex());
				if (pipeline == null)
				{
					continue;
				}

				commandBuffer.BindPipeline(pipeline);

				TrianglePushConstants pushConstants = BuildPushConstants(drawItem, material, textureIndex);
				commandBuffer.PushConstants(pushConstantStages, in pushConstants);
				commandBuffer.Draw(TriangleVertexCount);
			}

			commandBuffer.EndRenderPass();
			context.Submit();
		}

		public override void Dispose()
		{
			foreach (GraphicsPipeline pipeline in pipelinesByVariant.Values)
			{
				pipeline.Dispose();
			}
			pipelinesByVariant.Clear();

			triangleVertexBuffer?.Dispose();
			triangleVertexBuffer = null;

			defaultTexture?.Dispose();
			defaultTexture = null;

			shader = null;
			defaultMaterial = null;

			resourcesReady = false;
			resourceCreationFailed = false;
		}

		/// <summary>
		/// Loads the compiled shader and creates the resources one frame needs,
		/// on first use - which is also the first frame after the graphics backend
		/// came up.
		/// </summary>
		private void EnsureResources()
		{
			if (resourcesReady || resourceCreationFailed)
			{
				return;
			}

			// The shader is what HLSLCC compiled from the .vsf file: one SPIR-V
			// module per stage, for every variant the build kept.
			shader = Shader.Load(DefaultShaderName);
			if (shader == null || !shader.IsValid)
			{
				FailResourceCreation("the compiled shader '" + DefaultShaderName + "' could not be loaded");
				return;
			}

			defaultMaterial = Material.Create(shader);
			if (defaultMaterial == null)
			{
				FailResourceCreation("the default material could not be created");
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

			resourcesReady = true;
			Debug.LogInfo("Forward2DRenderPipeline: shader '" + shader.ShaderName + "' ready ("
				+ shader.VariantCount + " variant(s), queue " + shader.RenderQueue + ").");

			for (int variantIndex = 0; variantIndex < shader.VariantCount; ++variantIndex)
			{
				string variantKey = shader.GetVariantKey(variantIndex);
				string vertexEntryPoint = shader.GetEntryPointName(variantIndex, ShaderStage.Vertex);
				string fragmentEntryPoint = shader.GetEntryPointName(variantIndex, ShaderStage.Fragment);
				ShaderStageReflectionSummary reflection = shader.GetStageReflection(variantIndex, ShaderStage.Fragment);

				Debug.LogInfo("  variant " + variantIndex + " '" + (variantKey.Length > 0 ? variantKey : "default")
					+ "': vertex '" + vertexEntryPoint + "', fragment '" + fragmentEntryPoint
					+ "', push constants " + reflection.PushConstantByteSize + " bytes.");
			}
		}

		/// <summary>
		/// The material a drawable renders with: the one it carries, or the
		/// pipeline's default - which is also handed to the component so later
		/// property writes reach the same material.
		/// </summary>
		private Material ResolveMaterial(RenderDrawItem drawItem)
		{
			if (drawItem.MaterialHandle != 0)
			{
				return new Material(drawItem.MaterialHandle);
			}

			// First frame of this drawable: give it the default material and carry
			// over the color mode the script may already have set.
			defaultMaterial!.SetFloat(Material.ColorModePropertyName, (float)drawItem.ColorMode);
			NativeApi.VspComponent_SetMaterial(drawItem.ComponentHandle, defaultMaterial.NativeHandle);
			return defaultMaterial;
		}

		/// <summary>
		/// Returns the pipeline of one shader variant, creating it on first use.
		/// Each variant has its own SPIR-V modules, so each needs its own
		/// pipeline; everything else about them is identical.
		/// </summary>
		private GraphicsPipeline? GetOrCreatePipeline(int variantIndex)
		{
			if (shader == null)
			{
				return null;
			}
			if (pipelinesByVariant.TryGetValue(variantIndex, out GraphicsPipeline? cachedPipeline))
			{
				return cachedPipeline;
			}

			if (variantIndex < 0 || variantIndex >= shader.VariantCount)
			{
				variantIndex = 0;
			}

			ShaderModule? vertexShader = CreateStageShader(variantIndex, ShaderStage.Vertex);
			ShaderModule? fragmentShader = CreateStageShader(variantIndex, ShaderStage.Fragment);
			if (vertexShader == null || fragmentShader == null)
			{
				vertexShader?.Dispose();
				fragmentShader?.Dispose();
				return null;
			}

			ShaderStageReflectionSummary reflection = shader.GetStageReflection(variantIndex, ShaderStage.Vertex);
			int pushConstantByteCount = (int)reflection.PushConstantByteSize;
			if (pushConstantByteCount <= 0)
			{
				pushConstantByteCount = Marshal.SizeOf<TrianglePushConstants>();
			}

			GraphicsPipeline? pipeline;
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
						Vertex2D.Stride,
						new VertexAttribute(0, 2, 0),    // position
						new VertexAttribute(1, 4, 8),    // color
						new VertexAttribute(2, 2, 24))   // uv
					.SetTopology(PrimitiveTopology.TriangleList)
					.SetBlendEnabled(true)
					.SetPushConstantByteCount((uint)pushConstantByteCount);

				pipeline = builder.Build();
			}

			// The shader modules are baked into the pipeline, so they can go.
			vertexShader.Dispose();
			fragmentShader.Dispose();

			if (pipeline == null || !pipeline.IsValid)
			{
				Debug.LogError("Forward2DRenderPipeline: the pipeline of shader variant " + variantIndex + " could not be created.");
				return null;
			}

			pipelinesByVariant[variantIndex] = pipeline;
			return pipeline;
		}

		/// <summary>
		/// Hands one stage's SPIR-V module to the backend and names its entry
		/// point, which is what the shader file declared with #pragma.
		/// </summary>
		private ShaderModule? CreateStageShader(int variantIndex, ShaderStage stage)
		{
			int byteCount = shader!.GetStageSpirvSize(variantIndex, stage);
			if (byteCount <= 0)
			{
				Debug.LogError("Forward2DRenderPipeline: variant " + variantIndex + " has no " + stage + " module.");
				return null;
			}

			byte[] spirvCode = new byte[byteCount];
			if (!shader.CopyStageSpirv(variantIndex, stage, spirvCode))
			{
				Debug.LogError("Forward2DRenderPipeline: the " + stage + " module of variant " + variantIndex + " could not be read.");
				return null;
			}

			ShaderModule compiledStageShader = new ShaderModule(stage, shader.GetEntryPointName(variantIndex, stage), spirvCode);
			if (!compiledStageShader.IsValid)
			{
				Debug.LogError("Forward2DRenderPipeline: the backend rejected the " + stage + " module of variant " + variantIndex + ".");
				return null;
			}
			return compiledStageShader;
		}

		/// <summary>
		/// Directory the build stages the compiled shaders into: a "Shaders"
		/// folder next to the executable.
		/// </summary>
		public static string GetShaderAssetDirectory()
		{
			byte[] directoryBuffer = new byte[1024];
			NativeApi.VspPlatform_GetExecutableDirectoryUtf8(directoryBuffer, directoryBuffer.Length);
			return System.IO.Path.Combine(Shader.ReadText(directoryBuffer), "Shaders");
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
		/// Translates a draw item and its material into the shader constants:
		/// where the triangle sits, which color it shows and how the material
		/// tints it.
		/// </summary>
		private static TrianglePushConstants BuildPushConstants(
			RenderDrawItem drawItem,
			Material material,
			uint textureIndex)
		{
			TrianglePushConstants pushConstants = default;
			pushConstants.PositionOffsetX = drawItem.Position.X;
			pushConstants.PositionOffsetY = drawItem.Position.Y;
			pushConstants.ColorMode = (int)drawItem.ColorMode;
			pushConstants.TextureIndex = textureIndex;
			pushConstants.OverrideColorA = 1.0f;

			// MultiColor keeps the vertex colors, so its override stays zero and
			// the fragment stage ignores it (uColorMode == 3).
			switch (drawItem.ColorMode)
			{
			case ColorMode.Red:
				pushConstants.OverrideColorR = 1.0f;
				break;

			case ColorMode.Blue:
				pushConstants.OverrideColorB = 1.0f;
				break;

			case ColorMode.Green:
				pushConstants.OverrideColorG = 1.0f;
				break;

			default:
				break;
			}

			// The material's tint; it reaches the image only in the variants that
			// declare the _TINT_ENABLED keyword, and white leaves it unchanged.
			Vector4 tint = material.GetVector(Material.TintPropertyName, Vector4.One);
			pushConstants.TintColorR = tint.X;
			pushConstants.TintColorG = tint.Y;
			pushConstants.TintColorB = tint.Z;
			pushConstants.TintColorA = tint.W;

			return pushConstants;
		}
	}
}
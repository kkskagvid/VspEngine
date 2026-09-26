using System;
using System.Collections.Generic;
using System.Numerics;
using System.Runtime.InteropServices;

namespace VspEngine.Rendering
{
	/// <summary>
	/// The push-constant block the engine's sky shader declares: the three gradient
	/// colours, the sun and the falloffs that shape them.
	///
	/// The layout and the explicit offsets must stay identical to the
	/// SkyboxPushConstants block in Engine/Shaders/Runtime/SkyboxCommon.hlsl - the
	/// reflection HLSLCC produced for that block is what the renderer reads back from
	/// the loaded shader, and what it sizes its pipeline with.
	///
	/// A sky needs no per-draw state of its own (it is one fullscreen triangle), so
	/// this block IS the sky: everything the shader shades comes from here, and
	/// everything here comes from the sky's material.
	/// </summary>
	[StructLayout(LayoutKind.Sequential, Pack = 4)]
	internal struct SkyboxPushConstants
	{
		public Vector4 ZenithColor;    // offset 0   (rgb = the colour overhead)
		public Vector4 HorizonColor;   // offset 16  (rgb = the colour at eye level)
		public Vector4 GroundColor;    // offset 32  (rgb = the colour below the horizon)
		public Vector4 SunDirection;   // offset 48  (xyz = the direction towards the sun, w = intensity)
		public Vector4 SunColor;       // offset 64  (rgb = the sun's colour, a = cosine of its angular radius)
		public Vector4 SkyShape;       // offset 80  (x = zenith falloff, y = below-horizon falloff, z = glow falloff)
	}

	/// <summary>
	/// The engine's sky: it loads the sky shader, builds its pipeline and paints the
	/// sky into every frame, in front of nothing and behind everything.
	///
	/// The sky is ONE fullscreen triangle, so the renderer owns no mesh, no vertex
	/// buffer and no per-object state - only the shader, one pipeline and the
	/// material the sky is drawn with. The material is the whole configuration
	/// (<see cref="RenderSettings.Skybox"/>); the engine's default one is created
	/// here, from the shader it ships, and is used until a game replaces it.
	///
	/// A game that supplies its own sky shader has to declare the same push-constant
	/// block and the same property names (<see cref="PropertyNames"/>): the engine
	/// reads the material by name and fills that block, which is the contract between
	/// the engine's frame and whatever draws its sky.
	/// </summary>
	internal static class SkyboxRenderer
	{
		/// <summary>Name of the sky shader the engine ships and loads by default.</summary>
		public const string DefaultShaderName = "Skybox";

		/// <summary>
		/// The properties the engine reads off a sky material, in the order the
		/// shader's own Properties block declares them. They are the contract a
		/// replacement sky shader has to keep.
		/// </summary>
		public static class PropertyNames
		{
			public const string ZenithColor = "_ZenithColor";
			public const string HorizonColor = "_HorizonColor";
			public const string GroundColor = "_GroundColor";
			public const string SunColor = "_SunColor";
			public const string SunDirection = "_SunDirection";
			public const string SunIntensity = "_SunIntensity";
			public const string SunAngularSize = "_SunAngularSize";
			public const string ZenithFalloff = "_ZenithFalloff";
			public const string GroundFalloff = "_GroundFalloff";
			public const string SunGlowFalloff = "_SunGlowFalloff";
		}

		/// <summary>Vertices the sky is drawn with: the corners of the triangle SV_VertexID generates.</summary>
		private const uint SkyboxVertexCount = 3;

		/// <summary>Degrees of arc one radian is worth; the angular size is authored in degrees.</summary>
		private const float DegreesToRadians = 0.01745329251994329577f;

		/// <summary>Bounces of the sun the shader has when a material says nothing.</summary>
		private const float DefaultSunAngularSizeDegrees = 3.5f;
		private const float DefaultSunIntensity = 1.0f;
		private const float DefaultZenithFalloff = 0.65f;
		private const float DefaultGroundFalloff = 0.5f;
		private const float DefaultSunGlowFalloff = 32.0f;

		// The shader and material the engine ships, created once and kept while the
		// graphics backend lives.
		private static Shader? defaultShader;
		private static Material? defaultMaterial;

		// One pipeline per sky shader: a game that replaces the material with one of
		// its own shaders gets a pipeline for it, and the engine's default keeps its
		// own.
		private static readonly Dictionary<uint, GraphicsPipeline> pipelinesByShader = new Dictionary<uint, GraphicsPipeline>();

		// Set once the engine has reported why the sky cannot be drawn, so a missing
		// shader is one line in the log rather than one per frame.
		private static bool hasReportedFailure;

		/// <summary>
		/// Creates another material of the engine's sky shader: the starting point of
		/// a game's own sky. Its values begin as the shader's defaults, exactly like
		/// any other material.
		/// </summary>
		public static Material? CreateMaterialFromDefaultShader()
		{
			Shader? shader = GetOrCreateDefaultShader();
			return shader != null ? Material.Create(shader) : null;
		}

		/// <summary>
		/// The material the engine's own sky is drawn with, created on first use from
		/// the shader the engine ships. Returns null - after saying why, once - when
		/// that shader is not there, which is a build that did not compile the
		/// engine's shaders into the run directory.
		/// </summary>
		public static Material? GetOrCreateDefaultMaterial()
		{
			if (defaultMaterial != null && defaultMaterial.IsValid)
			{
				return defaultMaterial;
			}

			Shader? shader = GetOrCreateDefaultShader();
			if (shader == null)
			{
				return null;
			}

			defaultMaterial = Material.Create(shader);
			if (defaultMaterial == null)
			{
				ReportFailure("the engine's default sky material could not be created");
				return null;
			}

			Debug.LogInfo("SkyboxRenderer: the engine's sky is ready (shader '" + defaultShader.ShaderName
				+ "', " + defaultShader.VariantCount + " variant(s)); RenderSettings.Skybox replaces it.");
			return defaultMaterial;
		}

		/// <summary>
		/// Paints the frame's sky: whatever <see cref="RenderSettings.Skybox"/> names,
		/// or the engine's default material, drawn as one fullscreen triangle. Called
		/// by the engine's frame driver, inside the frame's render pass and BEFORE the
		/// game's passes, so the sky is behind everything a pipeline draws.
		///
		/// A frame with no camera is not drawn into - the shader unprojects with the
		/// camera block - and no sky is painted for it.
		/// </summary>
		public static void DrawSky(CommandBuffer commandBuffer)
		{
			Material? material = RenderSettings.ResolvedSkybox;
			if (material == null || !material.IsValid)
			{
				return;
			}

			GraphicsPipeline? pipeline = GetOrCreatePipeline(material);
			if (pipeline == null)
			{
				return;
			}

			commandBuffer.BindPipeline(pipeline);

			// The whole sky is the push-constant block, and the block is the material.
			SkyboxPushConstants pushConstants = BuildPushConstants(material);
			commandBuffer.PushConstants(ShaderStageFlags.Vertex | ShaderStageFlags.Fragment, in pushConstants);

			commandBuffer.Draw(SkyboxVertexCount);
		}

		/// <summary>
		/// Releases the shader, the material and every pipeline. The engine calls it
		/// while the graphics backend is still alive.
		/// </summary>
		public static void ReleaseResources()
		{
			foreach (GraphicsPipeline pipeline in pipelinesByShader.Values)
			{
				pipeline.Dispose();
			}
			pipelinesByShader.Clear();

			defaultMaterial?.Destroy();
			defaultMaterial = null;
			defaultShader = null;
			hasReportedFailure = false;
		}

		/// <summary>
		/// The pipeline that draws a material's sky: no vertex input at all (the
		/// triangle comes from SV_VertexID), no culling, and a depth test that always
		/// passes without ever writing depth - which is what lets the sky be painted
		/// first and still leave the depth buffer to the scene.
		/// </summary>
		private static GraphicsPipeline? GetOrCreatePipeline(Material material)
		{
			Shader? shader = material.Shader;
			if (shader == null || !shader.IsValid)
			{
				ReportFailure("a sky material without a shader was set");
				return null;
			}

			if (pipelinesByShader.TryGetValue(shader.NativeHandle, out GraphicsPipeline? cachedPipeline))
			{
				return cachedPipeline.IsValid ? cachedPipeline : null;
			}

			ShaderModule? vertexShader = CreateStageShader(shader, ShaderStage.Vertex);
			ShaderModule? fragmentShader = CreateStageShader(shader, ShaderStage.Fragment);
			if (vertexShader == null || fragmentShader == null)
			{
				vertexShader?.Dispose();
				fragmentShader?.Dispose();
				ReportFailure("the sky shader '" + shader.ShaderName + "' is missing a stage");
				return null;
			}

			int pushConstantByteCount = ResolvePushConstantByteCount(shader, Marshal.SizeOf<SkyboxPushConstants>());

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
					.SetVertexStride(0)                          // no vertex buffer: the corners come from SV_VertexID
					.SetTopology(PrimitiveTopology.TriangleList)
					.SetCullMode(CullMode.None)
					.SetDepthTest(true, false, CompareOperation.Always)
					.SetBlendEnabled(false)
					.SetPushConstantByteCount((uint)pushConstantByteCount);

				pipeline = builder.Build();
			}

			vertexShader.Dispose();
			fragmentShader.Dispose();

			if (pipeline == null || !pipeline.IsValid)
			{
				ReportFailure("the sky pipeline of shader '" + shader.ShaderName + "' could not be created");
				return null;
			}

			pipelinesByShader[shader.NativeHandle] = pipeline;
			return pipeline;
		}

		/// <summary>
		/// Reads one stage of one variant of a sky shader out of the container the
		/// compiler wrote, and hands it to the backend as a module.
		/// </summary>
		private static ShaderModule? CreateStageShader(Shader shader, ShaderStage stage)
		{
			// The sky has one variant, so variant 0 is the whole shader.
			const int variantIndex = 0;

			int byteCount = shader.GetStageSpirvSize(variantIndex, stage);
			if (byteCount <= 0)
			{
				return null;
			}

			byte[] spirvCode = new byte[byteCount];
			if (!shader.CopyStageSpirv(variantIndex, stage, spirvCode))
			{
				return null;
			}

			ShaderModule module = new ShaderModule(stage, shader.GetEntryPointName(variantIndex, stage), spirvCode);
			return module.IsValid ? module : null;
		}

		/// <summary>
		/// Bytes of push-constant data the pipeline must carry: the larger of what the
		/// shader's two stages read, because one pipeline shares one range between
		/// them. The sky's vertex stage generates the triangle and reads nothing, while
		/// its fragment stage reads the whole block.
		/// </summary>
		private static int ResolvePushConstantByteCount(Shader shader, int fallbackByteCount)
		{
			const int variantIndex = 0;
			ShaderStageReflectionSummary vertexReflection = shader.GetStageReflection(variantIndex, ShaderStage.Vertex);
			ShaderStageReflectionSummary fragmentReflection = shader.GetStageReflection(variantIndex, ShaderStage.Fragment);

			int byteCount = (int)Math.Max(vertexReflection.PushConstantByteSize, fragmentReflection.PushConstantByteSize);
			return byteCount > 0 ? byteCount : fallbackByteCount;
		}

		/// <summary>
		/// Builds the frame's sky from a material: every value the shader reads comes
		/// off the material, so replacing the material replaces the sky with no other
		/// change anywhere.
		/// </summary>
		private static SkyboxPushConstants BuildPushConstants(Material material)
		{
			// How large the sun looks is authored in degrees - the unit a person states
			// an angle in - and carried to the shader as the cosine of that angle,
			// because the shader compares it with a dot product.
			float angularSizeDegrees = material.GetFloat(PropertyNames.SunAngularSize, DefaultSunAngularSizeDegrees);
			float cosAngularRadius = MathF.Cos(angularSizeDegrees * DegreesToRadians);
			if (cosAngularRadius < 0.0f)
			{
				cosAngularRadius = 0.0f;
			}

			Vector4 sunDirection = material.GetVector(PropertyNames.SunDirection, new Vector4(0.0f, 1.0f, 0.0f, 0.0f));
			Vector3 normalizedSunDirection = NativeMath.Normalize(new Vector3(sunDirection.X, sunDirection.Y, sunDirection.Z));

			SkyboxPushConstants pushConstants = default;
			pushConstants.ZenithColor = material.GetVector(PropertyNames.ZenithColor, new Vector4(0.16f, 0.34f, 0.66f, 1.0f));
			pushConstants.HorizonColor = material.GetVector(PropertyNames.HorizonColor, new Vector4(0.62f, 0.74f, 0.90f, 1.0f));
			pushConstants.GroundColor = material.GetVector(PropertyNames.GroundColor, new Vector4(0.26f, 0.31f, 0.40f, 1.0f));
			pushConstants.SunColor = material.GetVector(PropertyNames.SunColor, new Vector4(1.0f, 0.95f, 0.84f, 1.0f));
			pushConstants.SunColor.W = cosAngularRadius;
			pushConstants.SunDirection = new Vector4(
				normalizedSunDirection,
				material.GetFloat(PropertyNames.SunIntensity, DefaultSunIntensity));
			pushConstants.SkyShape = new Vector4(
				material.GetFloat(PropertyNames.ZenithFalloff, DefaultZenithFalloff),
				material.GetFloat(PropertyNames.GroundFalloff, DefaultGroundFalloff),
				material.GetFloat(PropertyNames.SunGlowFalloff, DefaultSunGlowFalloff),
				0.0f);
			return pushConstants;
		}

		/// <summary>
		/// Loads the shader the engine ships for its sky, once. The container is read
		/// from the run directory's Shaders folder, exactly like a game's would be.
		/// </summary>
		private static Shader? GetOrCreateDefaultShader()
		{
			if (defaultShader != null && defaultShader.IsValid)
			{
				return defaultShader;
			}

			defaultShader = Shader.Load(DefaultShaderName);
			if (defaultShader == null || !defaultShader.IsValid)
			{
				ReportFailure("the engine's sky shader '" + DefaultShaderName + "' could not be loaded");
				return null;
			}
			return defaultShader;
		}

		private static void ReportFailure(string reason)
		{
			if (hasReportedFailure)
			{
				return;
			}

			hasReportedFailure = true;
			Debug.LogError("SkyboxRenderer: " + reason + "; frames are drawn without a sky.");
		}
	}
}

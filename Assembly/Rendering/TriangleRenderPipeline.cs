using System;
using System.Collections.Generic;
using System.Numerics;
using System.Runtime.InteropServices;

using VspEngine;
using VspEngine.Rendering;

namespace Assembly.Rendering
{
	/// <summary>
	/// The push-constant block the demo shader declares: the draw's world matrix,
	/// the flat color, the material tint, the light and the bindless texture slot.
	///
	/// The layout and the explicit offsets must stay identical to the
	/// PassPushConstants block in Assembly/Shaders/Triangle2DCommon.hlsl - the
	/// reflection HLSLCC produced for that block is what the pipeline reads back
	/// from the loaded shader.
	/// </summary>
	[StructLayout(LayoutKind.Sequential, Pack = 4)]
	internal struct DemoPushConstants
	{
		public Matrix4x4 WorldMatrix;    // offset 0   (row_major in the shader)
		public Vector4 BaseColor;        // offset 64
		public Vector4 TintColor;        // offset 80
		public Vector4 LightDirection;   // offset 96  (xyz = travel direction, w = intensity)
		public int ColorMode;            // offset 112
		public uint TextureIndex;        // offset 116
		public float Roughness;          // offset 120
		public float SpecularStrength;   // offset 124
	}

	/// <summary>
	/// The game's forward pipeline: one pass that clears the background and draws
	/// the demo scene - a camera, a cube and the colored triangle - in 3D.
	///
	/// This is GAME code. The engine ships no pipeline of its own, so rendering -
	/// and with it every shader load - starts here: the pipeline creates the
	/// scene objects it draws (a camera and a cube), calls <see cref="Shader.Load"/>
	/// for the game's compiled shader, builds one graphics pipeline per shader
	/// VARIANT and draws with depth testing, so nearer surfaces hide farther ones.
	///
	/// The "2D" content of the demo is the colored triangle: it is a mesh in the
	/// same 3D scene, drawn through the same camera and the same depth buffer.
	/// The engine has no separate 2D path.
	/// </summary>
	public sealed class TriangleRenderPipeline : RenderPipeline
	{
		/// <summary>Background the frame clears to.</summary>
		public static readonly Color BackgroundColor = new Color(0.06f, 0.06f, 0.10f, 1.0f);

		/// <summary>Name of the compiled shader this pipeline draws with.</summary>
		public const string ShaderName = "Triangle2D";

		/// <summary>Where the camera sits and which way it looks.</summary>
		public static readonly Vector3 CameraPosition = new Vector3(0.0f, 0.0f, 6.0f);

		/// <summary>How far behind the triangle the cube sits, in world units.</summary>
		public const float CubeDistance = 3.0f;

		/// <summary>Color the cube is drawn with; dark, so the demo's colored triangle stays the subject.</summary>
		public static readonly Vector3 CubeColor = new Vector3(0.10f, 0.10f, 0.14f);

		/// <summary>Direction the demo light travels (it comes from the camera and above).</summary>
		public static readonly Vector3 LightDirection = new Vector3(-0.35f, -0.45f, -1.0f);

		private const uint WhiteTextureSize = 8;

		private Shader? shader;
		private Material? defaultMaterial;
		private Material? cubeMaterial;
		private Camera? camera;
		private GameObject? cameraObject;
		private GameObject? cubeObject;
		private VertexBuffer? triangleVertexBuffer;
		private IndexBuffer? triangleIndexBuffer;
		private VertexBuffer? cubeVertexBuffer;
		private IndexBuffer? cubeIndexBuffer;
		private Texture2D? whiteTexture;

		private DemoMesh? triangleMesh;
		private DemoMesh? cubeMesh;

		// One graphics pipeline per shader variant, built on first use.
		private readonly Dictionary<int, GraphicsPipeline> pipelinesByVariant = new Dictionary<int, GraphicsPipeline>();

		private bool resourcesReady;
		private bool resourceCreationFailed;

		/// <summary>The shader the pipeline loaded, once it has one.</summary>
		public Shader? Shader => shader;

		/// <summary>The material given to drawables that do not carry one.</summary>
		public Material? DefaultMaterial => defaultMaterial;

		/// <summary>The camera the demo renders from.</summary>
		public Camera? Camera => camera;

		/// <summary>The cube the demo draws behind the triangle, once it exists.</summary>
		public GameObject? Cube => cubeObject;

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

			// The whole frame renders from this camera: the backend fills the
			// engine's camera uniform buffer with its matrices and its lens.
			commandBuffer.SetCamera(camera!);

			commandBuffer.BeginRenderPass(BackgroundColor);
			commandBuffer.SetViewport(0.0f, 0.0f, context.BackbufferWidth, context.BackbufferHeight);
			commandBuffer.SetScissor(0, 0, (uint)context.BackbufferWidth, (uint)context.BackbufferHeight);

			ShaderStageFlags pushConstantStages = ShaderStageFlags.Vertex | ShaderStageFlags.Fragment;
			uint textureIndex = (uint)whiteTexture!.BindlessSlot;

			// The cube first: it is the farther object, so drawing it early makes
			// the depth test do the visible work when the triangle moves behind it.
			DrawCube(commandBuffer, pushConstantStages, textureIndex);

			// Then every drawable the engine gathered: one colored triangle each.
			foreach (RenderDrawItem drawItem in context.DrawItems)
			{
				Material material = ResolveMaterial(drawItem);
				GraphicsPipeline? pipeline = GetOrCreatePipeline(material.ResolveVariantIndex());
				if (pipeline == null)
				{
					continue;
				}

				commandBuffer.BindPipeline(pipeline);
				commandBuffer.BindVertexBuffer(triangleVertexBuffer!);
				commandBuffer.BindIndexBuffer(triangleIndexBuffer!);

				DemoPushConstants pushConstants = BuildTriangleConstants(drawItem, material, textureIndex);
				commandBuffer.PushConstants(pushConstantStages, in pushConstants);
				commandBuffer.DrawIndexed((uint)triangleMesh!.Indices.Length);
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
			triangleIndexBuffer?.Dispose();
			triangleIndexBuffer = null;
			cubeVertexBuffer?.Dispose();
			cubeVertexBuffer = null;
			cubeIndexBuffer?.Dispose();
			cubeIndexBuffer = null;

			whiteTexture?.Dispose();
			whiteTexture = null;

			// The scene objects the pipeline created go with it; the objects a
			// script owns belong to the scene and stay.
			cubeObject?.Destroy();
			cubeObject = null;
			cameraObject?.Destroy();
			cameraObject = null;
			camera = null;

			shader = null;
			defaultMaterial = null;
			cubeMaterial = null;

			resourcesReady = false;
			resourceCreationFailed = false;
		}

		/// <summary>
		/// Loads the game's compiled shader and creates the resources the demo
		/// scene needs, on first use - which is also the first frame after the
		/// graphics backend came up.
		/// </summary>
		private void EnsureResources()
		{
			if (resourcesReady || resourceCreationFailed)
			{
				return;
			}

			// The shader is what HLSLCC compiled from this game's .vsf file: one
			// SPIR-V module per stage, for every variant the build kept, packaged
			// in the shader container the engine reads.
			shader = Shader.Load(ShaderName);
			if (shader == null || !shader.IsValid)
			{
				FailResourceCreation("the compiled shader '" + ShaderName + "' could not be loaded");
				return;
			}

			defaultMaterial = Material.Create(shader);
			cubeMaterial = Material.Create(shader);
			if (defaultMaterial == null || cubeMaterial == null)
			{
				FailResourceCreation("the demo materials could not be created");
				return;
			}

			// The white texture keeps the demo colors exact while the bindless
			// array sampling is exercised.
			whiteTexture = Texture2D.CreateWhite(WhiteTextureSize, WhiteTextureSize);
			if (whiteTexture == null || !whiteTexture.IsValid)
			{
				FailResourceCreation("the white texture could not be created");
				return;
			}

			if (!CreateMeshes() || !CreateSceneObjects())
			{
				return;
			}

			resourcesReady = true;
			Debug.LogInfo("TriangleRenderPipeline: shader '" + shader.ShaderName + "' ready ("
				+ shader.VariantCount + " variant(s), queue " + shader.RenderQueue + ").");
			Debug.LogInfo("TriangleRenderPipeline: 3D scene ready (camera at "
				+ CameraPosition.X + ", " + CameraPosition.Y + ", " + CameraPosition.Z
				+ "; cube " + CubeDistance + " units behind the triangle).");

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

		/// <summary>Uploads the two meshes the demo draws.</summary>
		private bool CreateMeshes()
		{
			triangleMesh = DemoMesh.CreateColoredTriangle();
			cubeMesh = DemoMesh.CreateUnitCube();

			triangleVertexBuffer = new VertexBuffer((uint)(triangleMesh.Vertices.Length * DemoVertex.Stride));
			triangleIndexBuffer = new IndexBuffer((uint)triangleMesh.Indices.Length);
			cubeVertexBuffer = new VertexBuffer((uint)(cubeMesh.Vertices.Length * DemoVertex.Stride));
			cubeIndexBuffer = new IndexBuffer((uint)cubeMesh.Indices.Length);

			if (!triangleVertexBuffer.IsValid || !triangleIndexBuffer.IsValid ||
				!cubeVertexBuffer.IsValid || !cubeIndexBuffer.IsValid)
			{
				FailResourceCreation("the demo mesh buffers could not be allocated");
				return false;
			}

			if (!triangleVertexBuffer.Update<DemoVertex>(triangleMesh.Vertices) ||
				!triangleIndexBuffer.Update(triangleMesh.Indices) ||
				!cubeVertexBuffer.Update<DemoVertex>(cubeMesh.Vertices) ||
				!cubeIndexBuffer.Update(cubeMesh.Indices))
			{
				FailResourceCreation("the demo mesh buffers could not be filled");
				return false;
			}

			return true;
		}

		/// <summary>
		/// Creates the objects the demo scene is made of. They belong to the game,
		/// not to the engine: the engine only stores them.
		/// </summary>
		private bool CreateSceneObjects()
		{
			cameraObject = GameObject.Create("DemoCamera");
			cubeObject = GameObject.Create("DemoCube");
			GameObject? rigObject = GameObject.Create("DemoRig");
			if (cameraObject == null || cubeObject == null || rigObject == null)
			{
				FailResourceCreation("the demo scene objects could not be created");
				return false;
			}

			// The cube hangs off a rig: the rig sits at the cube's distance and
			// the cube sits at its rig's origin, so the scene has a parent and a
			// child - local and world coordinates that differ, which is what a
			// saved scene records side by side.
			rigObject.Transform.Position = new Vector3(0.0f, 0.0f, -CubeDistance);
			cubeObject.Transform.SetParent(rigObject.Transform, false);

			cameraObject.Transform.Position = CameraPosition;

			camera = Camera.Create(cameraObject);
			if (camera == null)
			{
				FailResourceCreation("the demo camera could not be created");
				return false;
			}

			// An orthographic camera whose half height is 1.5 world units: the
			// triangle (1.1 units tall) fills about a third of the screen and
			// world units stay square. The physical camera is a 50 mm lens on a
			// full-frame sensor, which is the "normal" lens of photography.
			camera.ProjectionMode = CameraProjectionMode.Orthographic;
			camera.OrthographicSize = 1.5f;
			camera.FieldOfView = 45.0f;
			camera.NearClipPlane = 0.1f;
			camera.FarClipPlane = 100.0f;
			camera.FocalLength = 50.0f;
			camera.SensorWidth = 36.0f;
			camera.SensorHeight = 24.0f;
			camera.Aperture = 2.8f;
			camera.FocusDistance = 6.0f;

			return true;
		}

		/// <summary>Draws the demo cube with the depth test on.</summary>
		private void DrawCube(CommandBuffer commandBuffer, ShaderStageFlags pushConstantStages, uint textureIndex)
		{
			if (cubeObject == null || cubeMesh == null)
			{
				return;
			}

			Material material = cubeMaterial!;
			GraphicsPipeline? pipeline = GetOrCreatePipeline(material.ResolveVariantIndex());
			if (pipeline == null)
			{
				return;
			}

			commandBuffer.BindPipeline(pipeline);
			commandBuffer.BindVertexBuffer(cubeVertexBuffer!);
			commandBuffer.BindIndexBuffer(cubeIndexBuffer!);

			DemoPushConstants pushConstants = default;
			pushConstants.WorldMatrix = cubeObject.Transform.LocalToWorldMatrix;
			pushConstants.BaseColor = new Vector4(CubeColor, 1.0f);
			pushConstants.TintColor = Vector4.One;
			pushConstants.LightDirection = new Vector4(LightDirection, 1.0f);

			// Any mode but 3 means "use BaseColor": the cube keeps its dark
			// material color instead of its per-face vertex colors, so it stays
			// scenery behind the colored triangle.
			pushConstants.ColorMode = 0;
			pushConstants.TextureIndex = textureIndex;
			pushConstants.Roughness = 0.55f;
			pushConstants.SpecularStrength = 0.35f;

			commandBuffer.PushConstants(pushConstantStages, in pushConstants);
			commandBuffer.DrawIndexed((uint)cubeMesh.Indices.Length);
		}

		/// <summary>
		/// The material a drawable renders with: the one it carries, or the
		/// pipeline's default - which is also handed to the component so later
		/// property writes reach the same material.
		/// </summary>
		private Material ResolveMaterial(RenderDrawItem drawItem)
		{
			Material? drawMaterial = drawItem.Material;
			if (drawMaterial != null)
			{
				return drawMaterial;
			}

			// First frame of this drawable: give it the default material.
			Material fallbackMaterial = defaultMaterial!;
			drawItem.AssignMaterial(fallbackMaterial);
			return fallbackMaterial;
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
				pushConstantByteCount = Marshal.SizeOf<DemoPushConstants>();
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
						DemoVertex.Stride,
						new VertexAttribute(0, 3, 0),     // position
						new VertexAttribute(1, 3, 12),    // normal
						new VertexAttribute(2, 4, 24),    // vertex color
						new VertexAttribute(3, 2, 40))    // uv
					.SetTopology(PrimitiveTopology.TriangleList)
					.SetCullMode(CullMode.Back)          // a closed 3D shape culls its back faces
					.SetDepthTest(true, true, CompareOperation.LessOrEqual)
					.SetBlendEnabled(true)
					.SetPushConstantByteCount((uint)pushConstantByteCount);

				pipeline = builder.Build();
			}

			// The shader modules are baked into the pipeline, so they can go.
			vertexShader.Dispose();
			fragmentShader.Dispose();

			if (pipeline == null || !pipeline.IsValid)
			{
				Debug.LogError("TriangleRenderPipeline: the pipeline of shader variant " + variantIndex + " could not be created.");
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
				Debug.LogError("TriangleRenderPipeline: variant " + variantIndex + " has no " + stage + " module.");
				return null;
			}

			byte[] spirvCode = new byte[byteCount];
			if (!shader.CopyStageSpirv(variantIndex, stage, spirvCode))
			{
				Debug.LogError("TriangleRenderPipeline: the " + stage + " module of variant " + variantIndex + " could not be read.");
				return null;
			}

			ShaderModule compiledStageShader = new ShaderModule(stage, shader.GetEntryPointName(variantIndex, stage), spirvCode);
			if (!compiledStageShader.IsValid)
			{
				Debug.LogError("TriangleRenderPipeline: the backend rejected the " + stage + " module of variant " + variantIndex + ".");
				return null;
			}
			return compiledStageShader;
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
				Debug.LogError("TriangleRenderPipeline: " + reason + "; frames fall back to a plain clear.");
			}
		}

		/// <summary>
		/// Translates a drawable and its material into the shader constants: where
		/// the triangle sits, which color it shows and how the material tints it.
		/// </summary>
		private static DemoPushConstants BuildTriangleConstants(
			RenderDrawItem drawItem,
			Material material,
			uint textureIndex)
		{
			TriangleColorMode colorMode = (TriangleColorMode)(int)material.GetFloat(
				TriangleMaterial.ColorModePropertyName, (float)TriangleColorMode.MultiColor);

			// The drawable's own place in the scene, with its parent chain
			// applied: the world matrix is what the vertex stage multiplies by.
			Matrix4x4 worldMatrix = drawItem.WorldMatrix;

			DemoPushConstants pushConstants = default;
			pushConstants.WorldMatrix = worldMatrix;
			pushConstants.BaseColor = new Vector4(1.0f, 1.0f, 1.0f, 1.0f);
			pushConstants.TintColor = Vector4.One;
			pushConstants.LightDirection = new Vector4(LightDirection, 1.0f);
			pushConstants.ColorMode = (int)colorMode;
			pushConstants.TextureIndex = textureIndex;
			pushConstants.Roughness = 1.0f;
			pushConstants.SpecularStrength = 0.0f;

			// MultiColor keeps the vertex colors, so the flat color stays black;
			// the fragment stage does not read it in that mode (ColorMode == 3).
			TriangleMaterial.GetFlatColor(
				colorMode, out pushConstants.BaseColor.X, out pushConstants.BaseColor.Y, out pushConstants.BaseColor.Z);

			// The material's tint; it reaches the image only in the variants that
			// declare the _TINT_ENABLED keyword, and white leaves it unchanged.
			Vector4 tint = material.GetVector(TriangleMaterial.TintPropertyName, Vector4.One);
			pushConstants.TintColor = tint;

			return pushConstants;
		}
	}
}

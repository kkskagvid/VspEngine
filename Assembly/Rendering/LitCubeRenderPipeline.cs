using System;
using System.Collections.Generic;
using System.Numerics;
using System.Runtime.InteropServices;

using Assembly.UI;

using VspEngine;
using VspEngine.Rendering;
using VspEngine.UI;

namespace Assembly.Rendering
{
	/// <summary>
	/// The game's frame, assembled as a RENDER GRAPH.
	///
	/// This is GAME code: the engine ships no pipeline of its own, so rendering -
	/// and with it every shader load - starts here. The flow is
	///
	///     Camera                       the camera the engine opens the frame with
	///     EnsureResources()            shader, meshes, camera rig, UI renderer
	///     graph.Compile() + Execute()   the passes, in dependency order
	///     submit
	///
	/// The FRAME around those passes is the engine's, not this pipeline's: the
	/// engine hands it the camera, opens its render pass, clears it and paints the
	/// sky into it before the first pass below runs - see RenderPipelineManager. A
	/// pipeline records passes INSIDE a frame; it neither opens nor closes one.
	///
	/// and the graph is what decides WHICH passes run and in which order:
	///
	///     Ground     draws the ground plate the cube moves over
	///     Cube       draws every drawable the engine gathered, as the lit cube
	///     Interface  draws the flat HUD on top, reading the font atlas
	///     Debug      a pass that exists but is switched off - the graph culls it
	///
	/// Everything the frame needs beyond the engine's clear and sky is a pass,
	/// which is what makes the frame readable and what lets a pass be switched off
	/// without touching the code around it.
	/// </summary>
	public sealed class LitCubeRenderPipeline : RenderPipeline
	{
		/// <summary>Name of the compiled 3D shader this pipeline draws with.</summary>
		public const string ShaderName = "LitCube";

		/// <summary>Background the frame clears to. The sky covers it, so this is what shows where the sky is not.</summary>
		public static readonly Color BackgroundColor = new Color(0.05f, 0.06f, 0.09f, 1.0f);

		/// <summary>
		/// Frame the demo replaces the engine's sky at. The whole of replacing a
		/// skybox is replacing the material (RenderSettings.Skybox), so this is the
		/// whole of the demo's sky code - and it is here so a run can show both skies.
		/// </summary>
		public const long SkyReplaceFrame = 400;

		/// <summary>
		/// Direction the demo light TRAVELS: straight down, i.e. shining from
		/// directly overhead at 90 degrees to the horizon. The top face of the
		/// cube therefore catches the full light and the four sides catch only
		/// the ambient fill.
		/// </summary>
		public static readonly Vector3 LightDirection = new Vector3(0.0f, -1.0f, 0.0f);

		/// <summary>Strength of that light (1 = the surface's own colour at full).</summary>
		public const float LightIntensity = 1.0f;

		/// <summary>Colour the surfaces that face away from the light still receive.</summary>
		public static readonly Vector3 AmbientColor = new Vector3(0.34f, 0.35f, 0.40f);

		/// <summary>How fast the cube spins, in degrees per second.</summary>
		public const float SpinDegreesPerSecond = 65.0f;

		/// <summary>Field of view of the demo camera, in degrees.</summary>
		public const float CameraFieldOfView = 55.0f;

		/// <summary>Half extent of the ground plate, in world units.</summary>
		public const float GroundHalfExtent = 8.0f;

		/// <summary>
		/// Height of the ground plate's TOP surface, in world units. It is the
		/// height a resting body settles at, so it is also what the acceptance
		/// test checks the cube against.
		/// </summary>
		public const float GroundSurfaceHeight = -0.5f;

		private const uint WhiteTextureSize = 8;

		// -------- Resources the pipeline owns --------
		private Shader? shader;
		private Material? cubeMaterial;
		private Texture2D? whiteTexture;

		private DemoMesh? cubeMesh;
		private DemoMesh? groundMesh;
		private VertexBuffer? cubeVertexBuffer;
		private IndexBuffer? cubeIndexBuffer;
		private VertexBuffer? groundVertexBuffer;
		private IndexBuffer? groundIndexBuffer;

		// -------- Scene the pipeline owns --------
		private GameObject? groundObject;
		private MeshCollider? groundCollider;

		private GameObject? cameraObject;
		private Camera? camera;
		private ThirdPersonCameraRig? cameraRig;

		// -------- Interface --------
		private UiRenderer? uiRenderer;
		private DemoHud? hud;

		// -------- Frame assembly --------
		private readonly RenderGraph frameGraph = new RenderGraph("LitCubeFrame");
		private readonly Dictionary<int, GraphicsPipeline> pipelinesByVariant = new Dictionary<int, GraphicsPipeline>();

		private bool resourcesReady;
		private bool resourceCreationFailed;
		private bool hasLoggedGraphSummary;
		private bool hasReplacedSky;

		/// <summary>The shader the pipeline loaded, once it has one.</summary>
		public Shader? Shader => shader;

		/// <summary>The third-person rig that places the camera.</summary>
		public ThirdPersonCameraRig? CameraRig => cameraRig;

		/// <summary>The interface the pipeline draws.</summary>
		public DemoHud? Hud => hud;

		public override void Render(ScriptableRenderContext context)
		{
			EnsureResources();

			if (!resourcesReady)
			{
				// Without resources the frame is still the engine's - it was opened,
				// cleared and given a sky before this ran - so there is nothing to do
				// but let it end.
				context.Submit();
				return;
			}

			// The draw list the engine gathered for this frame: what the Cube
			// pass draws, and where the status line reads the cube's position.
			frameDrawItems = context.DrawItems;

			int backbufferWidth = context.BackbufferWidth;
			int backbufferHeight = context.BackbufferHeight;

			// The camera follows where the cube IS in the frame being built, which
			// is where the physics step left it - not where the script saw it
			// before that step ran. The draw list is the scene as the frame will
			// draw it, so the camera can never be one step behind the object it
			// follows.
			cameraRig?.SetTarget(FindCubePosition());
			cameraRig?.Update(Time.DeltaTime);

			// The interface reacts to the pointer, before a single command is
			// recorded.
			UpdateInterface(backbufferWidth, backbufferHeight);

			// The render pass, its clear colour and its sky are the ENGINE's: this
			// pipeline records passes INSIDE the frame it was handed, and the engine
			// closes it when this method returns. The camera is the one the frame was
			// given (see Camera), so a shader reads the same view the sky was drawn
			// with.
			CommandBuffer commandBuffer = context.CommandBuffer;

			ReplaceSkyMaterialWhenDue();

			BuildFrameGraph(backbufferWidth, backbufferHeight);

			if (frameGraph.Compile())
			{
				frameGraph.Execute(context);

				if (!hasLoggedGraphSummary)
				{
					hasLoggedGraphSummary = true;
					Debug.LogInfo(frameGraph.GetDebugSummary());
				}
			}

			context.Submit();
		}

		/// <summary>
		/// Replaces the engine's sky with a sky of the game's, once, at a known frame.
		///
		/// Nothing here touches a pass, a shader or a pipeline: a sky IS a material,
		/// and putting a different one in RenderSettings.Skybox is the whole of
		/// replacing it. This one is another material of the ENGINE's sky shader with
		/// the values of a dusk; a game wanting a wholly different sky would write its
		/// own shader and hand over a material of that instead.
		/// </summary>
		private void ReplaceSkyMaterialWhenDue()
		{
			if (hasReplacedSky || Time.FrameCount < SkyReplaceFrame)
			{
				return;
			}

			hasReplacedSky = true;

			Material? duskSky = RenderSettings.CreateSkyboxMaterial();
			if (duskSky == null)
			{
				Debug.LogError("LitCubeRenderPipeline: the replacing sky material could not be created.");
				return;
			}

			duskSky.SetVector(SkyboxPropertyNames.ZenithColor, new Vector4(0.24f, 0.16f, 0.38f, 1.0f));
			duskSky.SetVector(SkyboxPropertyNames.HorizonColor, new Vector4(0.95f, 0.52f, 0.28f, 1.0f));
			duskSky.SetVector(SkyboxPropertyNames.GroundColor, new Vector4(0.30f, 0.19f, 0.22f, 1.0f));
			duskSky.SetVector(SkyboxPropertyNames.SunColor, new Vector4(1.0f, 0.78f, 0.45f, 1.0f));

			RenderSettings.Skybox = duskSky;

			Debug.Log("LitCubeRenderPipeline: the sky was replaced at frame " + Time.FrameCount
				+ " by putting a material of the game's in RenderSettings.Skybox.");
		}

		/// <summary>
		/// The names the engine's sky shader gives its values. A material of another
		/// shader has to declare the same ones for the engine to draw it.
		/// </summary>
		private static class SkyboxPropertyNames
		{
			public const string ZenithColor = "_ZenithColor";
			public const string HorizonColor = "_HorizonColor";
			public const string GroundColor = "_GroundColor";
			public const string SunColor = "_SunColor";
		}

		/// <summary>
		/// The camera the frame is rendered from. The engine asks for it before it
		/// opens the frame, which is where a pipeline that builds its resources on
		/// demand builds them - so the first frame already has a camera and a sky.
		/// </summary>
		public override Camera? Camera
		{
			get
			{
				EnsureResources();
				return camera;
			}
		}

		public override void Dispose()
		{
			foreach (GraphicsPipeline pipeline in pipelinesByVariant.Values)
			{
				pipeline.Dispose();
			}
			pipelinesByVariant.Clear();

			cubeVertexBuffer?.Dispose();
			cubeVertexBuffer = null;
			cubeIndexBuffer?.Dispose();
			cubeIndexBuffer = null;
			groundVertexBuffer?.Dispose();
			groundVertexBuffer = null;
			groundIndexBuffer?.Dispose();
			groundIndexBuffer = null;

			// Destroying the ground object releases its collider with it.
			groundCollider = null;
			groundObject?.Destroy();
			groundObject = null;

			whiteTexture?.Dispose();
			whiteTexture = null;

			uiRenderer?.Dispose();
			uiRenderer = null;
			hud?.Dispose();
			hud = null;

			ThirdPersonCameraRig.Detach();
			cameraRig = null;
			cameraObject?.Destroy();
			cameraObject = null;
			camera = null;

			shader = null;
			cubeMaterial = null;

			resourcesReady = false;
			resourceCreationFailed = false;
			hasLoggedGraphSummary = false;
		}

		// -----------------------------------------------------------------
		// Resource creation
		// -----------------------------------------------------------------

		private void EnsureResources()
		{
			if (resourcesReady || resourceCreationFailed)
			{
				return;
			}

			shader = Shader.Load(ShaderName);
			if (shader == null || !shader.IsValid)
			{
				FailResourceCreation("the compiled shader '" + ShaderName + "' could not be loaded");
				return;
			}

			cubeMaterial = Material.Create(shader);
			if (cubeMaterial == null)
			{
				FailResourceCreation("the cube material could not be created");
				return;
			}
			cubeMaterial.SetVector(LitCubeMaterial.BaseColorPropertyName, Vector4.One);

			// The demo textures nothing, but the bindless array is still exercised:
			// a white texture keeps the sampled value exact.
			whiteTexture = Texture2D.CreateWhite(WhiteTextureSize, WhiteTextureSize);
			if (whiteTexture == null || !whiteTexture.IsValid)
			{
				FailResourceCreation("the white texture could not be created");
				return;
			}

			if (!CreateMeshes() || !CreateCamera() || !CreateInterface())
			{
				return;
			}

			// The collision code is asked a few questions whose answers are
			// arithmetic before the demo relies on it for anything.
			PhysicsSelfTest.Run();

			// The frame clears to the colour the game asks for; the engine's frame
			// driver reads it. Setting it once is enough - it is a setting, not a
			// command.
			RenderSettings.BackgroundColor = BackgroundColor;

			resourcesReady = true;
			Debug.LogInfo("LitCubeRenderPipeline: shader '" + shader.ShaderName + "' ready ("
				+ shader.VariantCount + " variant(s), queue " + shader.RenderQueue + ").");
			Debug.LogInfo("LitCubeRenderPipeline: 3D scene ready (cube spins "
				+ SpinDegreesPerSecond + " deg/s; light shines straight down from ("
				+ LightDirection.X + ", " + LightDirection.Y + ", " + LightDirection.Z + ")).");
			Debug.LogInfo("LitCubeRenderPipeline: the frame's sky is the engine's (RenderSettings.Skybox is "
				+ (RenderSettings.Skybox == null ? "the engine's default" : "a material the game replaced") + ").");
		}

		private bool CreateMeshes()
		{
			cubeMesh = DemoMesh.CreateUnitCube();
			groundMesh = DemoMesh.CreateGroundPlane(GroundHalfExtent);

			cubeVertexBuffer = new VertexBuffer((uint)(cubeMesh.Vertices.Length * DemoVertex.Stride));
			cubeIndexBuffer = new IndexBuffer((uint)cubeMesh.Indices.Length);
			groundVertexBuffer = new VertexBuffer((uint)(groundMesh.Vertices.Length * DemoVertex.Stride));
			groundIndexBuffer = new IndexBuffer((uint)groundMesh.Indices.Length);

			if (!cubeVertexBuffer.IsValid || !cubeIndexBuffer.IsValid ||
				!groundVertexBuffer.IsValid || !groundIndexBuffer.IsValid)
			{
				FailResourceCreation("the demo mesh buffers could not be allocated");
				return false;
			}

			if (!cubeVertexBuffer.Update<DemoVertex>(cubeMesh.Vertices) ||
				!cubeIndexBuffer.Update(cubeMesh.Indices) ||
				!groundVertexBuffer.Update<DemoVertex>(groundMesh.Vertices) ||
				!groundIndexBuffer.Update(groundMesh.Indices))
			{
				FailResourceCreation("the demo mesh buffers could not be filled");
				return false;
			}

			return CreateGround();
		}

		/// <summary>
		/// Gives the ground plate a game object and a MESH COLLIDER.
		///
		/// The collider is built from the very vertices and indices the plate is
		/// drawn with, so what the simulation collides with IS the surface on
		/// screen - there is no second, hand-written description of the same
		/// shape that could drift away from the first one. The object sits at
		/// GroundSurfaceHeight, which is where the plate's surface therefore is
		/// and where a body that lands on it comes to rest.
		/// </summary>
		private bool CreateGround()
		{
			if (groundMesh == null)
			{
				return true;
			}

			groundObject = GameObject.Create("DemoGround");
			if (groundObject == null)
			{
				FailResourceCreation("the ground object could not be created");
				return false;
			}
			groundObject.Transform.Position = new Vector3(0.0f, GroundSurfaceHeight, 0.0f);

			groundCollider = MeshCollider.Create(groundObject);
			if (groundCollider == null)
			{
				FailResourceCreation("the ground mesh collider could not be created");
				return false;
			}

			Vector3[] meshVertices = new Vector3[groundMesh.Vertices.Length];
			for (int vertexIndex = 0; vertexIndex < groundMesh.Vertices.Length; ++vertexIndex)
			{
				meshVertices[vertexIndex] = groundMesh.Vertices[vertexIndex].Position;
			}

			if (!groundCollider.SetMesh(meshVertices, groundMesh.Indices))
			{
				FailResourceCreation("the ground mesh collider refused its mesh");
				return false;
			}

			// A floor does not store energy: a body that lands on it keeps the
			// bounce its own collider asks for and no more.
			groundCollider.Restitution = 0.0f;
			groundCollider.Friction = 0.5f;

			Debug.LogInfo("LitCubeRenderPipeline: ground ready (mesh collider with "
				+ groundCollider.TriangleCount + " triangle(s) at y "
				+ FormatFloat(GroundSurfaceHeight) + ").");
			return true;
		}

		/// <summary>
		/// Creates the camera the frame renders from and hands it to the
		/// third-person rig, which is what makes it FOLLOW the cube: the script
		/// reports where the cube is and how the pointer moved, the rig turns that
		/// into a place and an orientation that always keeps the cube centred.
		/// </summary>
		private bool CreateCamera()
		{
			cameraObject = GameObject.Create("DemoCamera");
			if (cameraObject == null)
			{
				FailResourceCreation("the demo camera object could not be created");
				return false;
			}

			camera = Camera.Create(cameraObject);
			if (camera == null)
			{
				FailResourceCreation("the demo camera could not be created");
				return false;
			}

			camera.ProjectionMode = CameraProjectionMode.Perspective;
			camera.FieldOfView = CameraFieldOfView;
			camera.NearClipPlane = 0.1f;
			camera.FarClipPlane = 200.0f;

			cameraRig = ThirdPersonCameraRig.Attach(cameraObject, camera);
			cameraRig.Update(0.0f);
			return true;
		}

		private bool CreateInterface()
		{
			uiRenderer = new UiRenderer();
			if (!uiRenderer.EnsureReady())
			{
				// A missing interface is not fatal to the frame: the cube still
				// renders, and the reason is already in the log.
				Debug.LogError("LitCubeRenderPipeline: the interface could not be created; the demo runs without a HUD.");
				uiRenderer = null;
				return true;
			}

			hud = new DemoHud();
			hud.SpinToggleRequested += OnSpinToggleRequested;
			hud.ResetRequested += OnResetRequested;
			hud.CameraResetRequested += OnCameraResetRequested;
			return true;
		}

		// -----------------------------------------------------------------
		// Interface
		// -----------------------------------------------------------------

		private void UpdateInterface(int backbufferWidth, int backbufferHeight)
		{
			if (hud == null)
			{
				return;
			}

			hud.Update(UiInputState.Capture(), backbufferWidth, backbufferHeight);

			// The status line is fed from the scene, not from the script: the
			// cube's world position is what the frame actually drew.
			Vector3 cubePosition = FindCubePosition();
			hud.ReportCubeState(cubePosition, CubeController.IsSpinningCounterClockwise);

			if (cameraRig != null)
			{
				hud.ReportCameraState(cameraRig.YawDegrees, cameraRig.PitchDegrees);
			}
		}

		/// <summary>World position of the first drawable the engine gathered, or the origin.</summary>
		private Vector3 FindCubePosition()
		{
			foreach (RenderDrawItem drawItem in frameDrawItems)
			{
				return drawItem.Position;
			}
			return Vector3.Zero;
		}

		private void OnSpinToggleRequested()
		{
			CubeController.ToggleSpinDirection();
		}

		private void OnResetRequested()
		{
			CubeController.RequestReset();
		}

		private void OnCameraResetRequested()
		{
			cameraRig?.ResetView();
		}

		// -----------------------------------------------------------------
		// Frame assembly (the render graph)
		// -----------------------------------------------------------------

		// The draw list of the frame being assembled; the graph's passes read it.
		private IReadOnlyList<RenderDrawItem> frameDrawItems = Array.Empty<RenderDrawItem>();

		private void BuildFrameGraph(int backbufferWidth, int backbufferHeight)
		{
			frameGraph.Reset();

			// The font atlas the interface samples is a REAL texture with a real
			// bindless slot, so the graph tracks it like any other resource: the
			// Interface pass below reads it, which is what keeps that pass from
			// being culled.
			int atlasSlot = hud?.Canvas.GetFont(hud.Canvas.DefaultTextPixelSize)?.AtlasBindlessSlot ?? -1;
			RenderGraphTextureHandle fontAtlas = frameGraph.CreateTexture("UiFontAtlas", 1024, 1024, atlasSlot);
			RenderGraphTextureHandle solidTexture = frameGraph.CreateTexture(
				"UiSolidTexture", 1, 1, uiRenderer?.SolidTextureBindlessSlot ?? -1);

			frameGraph.AddPass("Ground")
				.SetKind(RenderGraphPassKind.Raster)
				.WriteBackBuffer()
				.SetExecute(DrawGroundPass)
				.Done();

			frameGraph.AddPass("Cube")
				.SetKind(RenderGraphPassKind.Raster)
				.WriteBackBuffer()
				.SetExecute(DrawCubePass)
				.Done();

			frameGraph.AddPass("Interface")
				.SetKind(RenderGraphPassKind.Raster)
				.WriteBackBuffer()
				.ReadTexture(fontAtlas)
				.ReadTexture(solidTexture)
				.SetExecute(DrawInterfacePass)
				.Done();

			// A pass that exists but is switched off: the graph culls it, and
			// nothing about the frame changes. Switching it on is one line.
			frameGraph.AddPass("DiagnosticsOverlay")
				.SetKind(RenderGraphPassKind.Custom)
				.WriteBackBuffer()
				.SetEnabled(false)
				.SetExecute(DrawDiagnosticsOverlayPass)
				.Done();
		}

		private void DrawGroundPass(RenderGraphContext graphContext)
		{
			CommandBuffer commandBuffer = graphContext.CommandBuffer;
			commandBuffer.SetViewport(0.0f, 0.0f, graphContext.BackbufferWidth, graphContext.BackbufferHeight);
			commandBuffer.SetScissor(0, 0, (uint)graphContext.BackbufferWidth, (uint)graphContext.BackbufferHeight);

			GraphicsPipeline? pipeline = GetOrCreatePipeline(0);
			if (pipeline == null || groundMesh == null)
			{
				return;
			}

			commandBuffer.BindPipeline(pipeline);
			commandBuffer.BindVertexBuffer(groundVertexBuffer!);
			commandBuffer.BindIndexBuffer(groundIndexBuffer!);

			// The plate is drawn from the transform of the object that carries its
			// mesh collider, so the surface on screen and the surface a body lands
			// on are the same surface by construction.
			Matrix4x4 groundMatrix = (groundObject != null)
				? groundObject.Transform.LocalToWorldMatrix
				: Matrix4x4.CreateTranslation(0.0f, GroundSurfaceHeight, 0.0f);
			LitCubePushConstants pushConstants = BuildPushConstants(groundMatrix, new Vector4(1.0f, 1.0f, 1.0f, 1.0f));
			commandBuffer.PushConstants(PushConstantStages, in pushConstants);
			commandBuffer.DrawIndexed((uint)groundMesh.Indices.Length);
		}

		private void DrawCubePass(RenderGraphContext graphContext)
		{
			CommandBuffer commandBuffer = graphContext.CommandBuffer;

			Material material = cubeMaterial!;
			GraphicsPipeline? pipeline = GetOrCreatePipeline(material.ResolveVariantIndex());
			if (pipeline == null || cubeMesh == null)
			{
				return;
			}

			commandBuffer.BindPipeline(pipeline);
			commandBuffer.BindVertexBuffer(cubeVertexBuffer!);
			commandBuffer.BindIndexBuffer(cubeIndexBuffer!);

			// Every drawable the engine gathered is one lit cube at its own world
			// matrix - the whole scene is one mesh and one pipeline.
			foreach (RenderDrawItem drawItem in frameDrawItems)
			{
				LitCubePushConstants pushConstants = BuildPushConstants(drawItem.WorldMatrix, Vector4.One);
				commandBuffer.PushConstants(PushConstantStages, in pushConstants);
				commandBuffer.DrawIndexed((uint)cubeMesh.Indices.Length);
			}
		}

		private void DrawInterfacePass(RenderGraphContext graphContext)
		{
			if (uiRenderer == null || hud == null)
			{
				return;
			}

			uiRenderer.Render(
				graphContext.CommandBuffer,
				hud.Canvas,
				graphContext.BackbufferWidth,
				graphContext.BackbufferHeight);
		}

		/// <summary>
		/// A pass that is switched off. Its body is real code - it would draw a
		/// wireframe box around every drawable - and the graph removes it from the
		/// frame before anything is recorded, which is the point of a render
		/// graph: a pass costs nothing while it is off.
		/// </summary>
		private void DrawDiagnosticsOverlayPass(RenderGraphContext graphContext)
		{
			CommandBuffer commandBuffer = graphContext.CommandBuffer;
			GraphicsPipeline? pipeline = GetOrCreatePipeline(0);
			if (pipeline == null || cubeMesh == null)
			{
				return;
			}

			commandBuffer.BindPipeline(pipeline);
			commandBuffer.BindVertexBuffer(cubeVertexBuffer!);
			commandBuffer.BindIndexBuffer(cubeIndexBuffer!);
			foreach (RenderDrawItem drawItem in frameDrawItems)
			{
				LitCubePushConstants pushConstants = BuildPushConstants(drawItem.WorldMatrix, new Vector4(1.0f, 0.0f, 0.0f, 1.0f));
				commandBuffer.PushConstants(PushConstantStages, in pushConstants);
				commandBuffer.DrawIndexed((uint)cubeMesh.Indices.Length);
			}
		}

		private static readonly ShaderStageFlags PushConstantStages = ShaderStageFlags.Vertex | ShaderStageFlags.Fragment;

		private static LitCubePushConstants BuildPushConstants(Matrix4x4 worldMatrix, Vector4 baseColor)
		{
			LitCubePushConstants pushConstants = default;
			pushConstants.WorldMatrix = worldMatrix;
			pushConstants.BaseColor = baseColor;
			pushConstants.LightDirection = new Vector4(LightDirection, LightIntensity);
			pushConstants.AmbientColor = new Vector4(AmbientColor, 0.0f);
			return pushConstants;
		}

		// -----------------------------------------------------------------
		// Pipelines
		// -----------------------------------------------------------------

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

			ShaderModule? vertexShader = CreateStageShader(shader, variantIndex, ShaderStage.Vertex);
			ShaderModule? fragmentShader = CreateStageShader(shader, variantIndex, ShaderStage.Fragment);
			if (vertexShader == null || fragmentShader == null)
			{
				vertexShader?.Dispose();
				fragmentShader?.Dispose();
				return null;
			}

			int pushConstantByteCount = ResolvePushConstantByteCount(shader, variantIndex, Marshal.SizeOf<LitCubePushConstants>());

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
						new VertexAttribute(2, 4, 24))    // vertex colour
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
				Debug.LogError("LitCubeRenderPipeline: the pipeline of shader variant " + variantIndex + " could not be created.");
				return null;
			}

			pipelinesByVariant[variantIndex] = pipeline;
			return pipeline;
		}

		/// <summary>
		/// <summary>
		/// Bytes of push-constant data a pipeline must be able to carry: the
		/// LARGER of what the shader's two stages read, because one pipeline
		/// shares one push-constant range between them. The sky is why this is a
		/// maximum rather than one stage's answer - its vertex stage reads no
		/// push constant at all while its fragment stage reads all 96 bytes.
		/// </summary>
		private static int ResolvePushConstantByteCount(Shader sourceShader, int variantIndex, int fallbackByteCount)
		{
			ShaderStageReflectionSummary vertexReflection = sourceShader.GetStageReflection(variantIndex, ShaderStage.Vertex);
			ShaderStageReflectionSummary fragmentReflection = sourceShader.GetStageReflection(variantIndex, ShaderStage.Fragment);

			int byteCount = (int)Math.Max(vertexReflection.PushConstantByteSize, fragmentReflection.PushConstantByteSize);
			return byteCount > 0 ? byteCount : fallbackByteCount;
		}

		/// <summary>
		/// Reads one stage of one variant out of a loaded shader and hands it to
		/// the backend as a module. Every pipeline the demo builds - the cube's,
		/// the sky's - goes through here, so a module can only ever come from a
		/// compiled shader.
		/// </summary>
		private static ShaderModule? CreateStageShader(Shader sourceShader, int variantIndex, ShaderStage stage)
		{
			int byteCount = sourceShader.GetStageSpirvSize(variantIndex, stage);
			if (byteCount <= 0)
			{
				Debug.LogError("LitCubeRenderPipeline: shader '" + sourceShader.ShaderName
					+ "' variant " + variantIndex + " has no " + stage + " module.");
				return null;
			}

			byte[] spirvCode = new byte[byteCount];
			if (!sourceShader.CopyStageSpirv(variantIndex, stage, spirvCode))
			{
				Debug.LogError("LitCubeRenderPipeline: the " + stage + " module of shader '" + sourceShader.ShaderName
					+ "' variant " + variantIndex + " could not be read.");
				return null;
			}

			ShaderModule compiledStageShader = new ShaderModule(
				stage, sourceShader.GetEntryPointName(variantIndex, stage), spirvCode);
			if (!compiledStageShader.IsValid)
			{
				Debug.LogError("LitCubeRenderPipeline: the backend rejected the " + stage
					+ " module of shader '" + sourceShader.ShaderName + "' variant " + variantIndex + ".");
				return null;
			}
			return compiledStageShader;
		}

		/// <summary>
		/// Formats a number for a log line with a fixed number of decimals and a
		/// point as the decimal separator, whatever locale the machine is set to:
		/// the acceptance run reads these lines.
		/// </summary>
		private static string FormatFloat(float value) =>
			value.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture);

		private void FailResourceCreation(string reason)
		{
			if (!resourceCreationFailed)
			{
				resourceCreationFailed = true;
				Debug.LogError("LitCubeRenderPipeline: " + reason + "; frames fall back to a plain clear.");
			}
		}
	}
}

using System;
using System.Collections.Generic;
using System.IO;
using VspEngine.Rendering;

namespace VspEngine.UI
{
	/// <summary>
	/// The ENGINE's interface: it owns the frame's UI, whatever the interface is
	/// made of, and it draws it inside the frame the engine opened - never
	/// beside it and never through a pass of the game's.
	///
	/// Where it sits in a frame (<see cref="RenderPipelineManager.Render"/>):
	///
	///     engine: gather the draw list, set the camera, open the render pass,
	///             paint the sky
	///     game:   the active pipeline records its own passes
	///     ENGINE: UiSystem.RenderFrame(...)   <- the interface, on top
	///     engine: close the render pass, submit
	///
	/// The interface is therefore drawn by the same render pass, against the same
	/// depth buffer, with the same command buffer as the scene - the UI is a
	/// consumer of the wrapped graphics API, exactly like a game's pipeline, and
	/// it goes through <see cref="UiRenderer"/> (one shader, one pipeline, one
	/// dynamic vertex and index buffer per frame) rather than around it.
	///
	/// TWO LAYOUT SOURCES, one job each:
	///
	///   * <see cref="LoadLayoutFromJson"/> reads a JSON DOCUMENT
	///     (<see cref="UiLayoutLoader"/>) and builds the declarative half: which
	///     elements exist, where they sit, how they look, which localisation key a
	///     static label shows. It is read once and drawn every frame;
	///   * <see cref="ImmediateCanvas"/> is the code half (the IMGUI side): the
	///     canvas a game builds and keeps in code - the live readout, the element
	///     that only exists in some state, the pointer marker.
	///
	/// Both are drawn in one frame, the layout first and the code canvas on top of
	/// it, so a game binds behaviour onto a layout instead of re-describing it.
	///
	/// Every failure is reported and turns into "this frame draws no interface";
	/// nothing here takes the frame down.
	/// </summary>
	public static class UiSystem
	{
		/// <summary>Directory a layout file name is resolved against, next to the executable.</summary>
		public const string LayoutDirectoryName = "Ui";

		/// <summary>Extension a layout file name gets when it names none.</summary>
		public const string LayoutFileExtension = ".json";

		// The canvases of one frame, in drawing order. Kept as a field so a frame
		// allocates nothing.
		private static readonly List<Canvas> frameCanvases = new List<Canvas>(2);

		private static UiRenderer? uiRenderer;
		private static Canvas? layoutCanvas;
		private static Canvas? immediateCanvas;

		/// <summary>
		/// The canvas the JSON layout described, or null while no layout was
		/// loaded. It belongs to the engine and is drawn before the immediate one.
		/// </summary>
		public static Canvas? LayoutCanvas => layoutCanvas;

		/// <summary>
		/// The canvas a game builds in code, drawn on top of the layout canvas, or
		/// null when the game draws no interface of its own.
		///
		/// The game owns the canvas (it is what the game puts its live widgets in);
		/// the engine only draws it.
		/// </summary>
		public static Canvas? ImmediateCanvas
		{
			get => immediateCanvas;
			set => immediateCanvas = value;
		}

		/// <summary>True once the engine's UI renderer (shader, pipeline, buffers) is ready.</summary>
		public static bool IsReady => uiRenderer != null && uiRenderer.IsReady;

		/// <summary>Bindless slot of the 1x1 white texture solid quads sample (-1 while not ready).</summary>
		public static int SolidTextureBindlessSlot => uiRenderer?.SolidTextureBindlessSlot ?? -1;

		// -----------------------------------------------------------------
		// Layout sources
		// -----------------------------------------------------------------

		/// <summary>
		/// Loads a JSON layout and makes it the engine's layout canvas, replacing
		/// the previous one. <paramref name="layoutFileName"/> is either an
		/// absolute path or a name resolved against the executable's
		/// <c>Ui</c> directory ("DemoHud" and "DemoHud.json" are the same file).
		///
		/// Returns false - with the reason already in the engine log - when the
		/// file is missing or is not a layout; the frame keeps drawing whatever it
		/// drew before.
		/// </summary>
		public static bool LoadLayoutFromJson(string layoutFileName)
		{
			if (string.IsNullOrEmpty(layoutFileName))
			{
				Debug.LogError("UiSystem: no layout file name was given.");
				return false;
			}

			string layoutFilePath = ResolveLayoutFilePath(layoutFileName);
			using JsonDocument layoutDocument = JsonDocument.Load(layoutFilePath);
			if (!layoutDocument.IsValid)
			{
				// The reader already logged why (missing file, malformed JSON).
				return false;
			}

			Canvas? loadedCanvas = UiLayoutLoader.BuildCanvas(layoutDocument);
			if (loadedCanvas == null)
			{
				return false;
			}

			ReleaseLayoutCanvas();
			layoutCanvas = loadedCanvas;

			Debug.LogInfo("UiSystem: the layout '" + layoutFilePath + "' is loaded ("
				+ CountElements(loadedCanvas) + " element(s)) and is drawn by the engine.");
			return true;
		}

		/// <summary>Drops the layout canvas; the next frame draws no layout.</summary>
		public static void UnloadLayout()
		{
			ReleaseLayoutCanvas();
		}

		/// <summary>
		/// The first element with the given name, in the layout canvas first and in
		/// the immediate one after it - which is how code binds behaviour onto a
		/// layout it did not build (a button's click, a label's text).
		/// </summary>
		public static UiElement? FindElement(string elementName)
		{
			if (string.IsNullOrEmpty(elementName))
			{
				return null;
			}

			UiElement? foundElement = layoutCanvas?.FindDescendant(elementName);
			return foundElement ?? immediateCanvas?.FindDescendant(elementName);
		}

		/// <summary>The named element as the type its caller expects, or null.</summary>
		public static TElement? FindElement<TElement>(string elementName) where TElement : UiElement =>
			FindElement(elementName) as TElement;

		// -----------------------------------------------------------------
		// The frame
		// -----------------------------------------------------------------

		/// <summary>
		/// Draws the frame's interface: every canvas is updated with this frame's
		/// input and its quads are recorded into the command buffer of the frame
		/// the engine opened. Called by <see cref="RenderPipelineManager"/> after
		/// the active pipeline's passes and before the render pass is closed.
		/// </summary>
		public static bool RenderFrame(CommandBuffer commandBuffer, int width, int height)
		{
			if (commandBuffer == null || width <= 0 || height <= 0)
			{
				return false;
			}

			frameCanvases.Clear();
			if (layoutCanvas != null)
			{
				frameCanvases.Add(layoutCanvas);
			}
			if (immediateCanvas != null && !ReferenceEquals(immediateCanvas, layoutCanvas))
			{
				frameCanvases.Add(immediateCanvas);
			}

			if (frameCanvases.Count == 0)
			{
				// No interface is a complete answer, not a failure.
				return true;
			}

			if (!EnsureRenderer())
			{
				// EnsureRenderer already said why; the interface is simply not drawn.
				return false;
			}

			// Input reacts before a single command is recorded: hover state, presses
			// and clicks are resolved here, and the draw list is built from the
			// result.
			UiInputState input = UiInputState.Capture();
			for (int canvasIndex = 0; canvasIndex < frameCanvases.Count; ++canvasIndex)
			{
				frameCanvases[canvasIndex].Update(input, width, height);
			}

			return uiRenderer!.Render(commandBuffer, frameCanvases, width, height);
		}

		/// <summary>
		/// Releases the device resources of the interface (shader, pipeline,
		/// buffers, textures). The engine calls it while the graphics backend is
		/// still alive; a canvas a game owns is only forgotten, never destroyed.
		/// </summary>
		public static void Release()
		{
			uiRenderer?.Dispose();
			uiRenderer = null;

			ReleaseLayoutCanvas();
			immediateCanvas = null;
			frameCanvases.Clear();
		}

		/// <summary>Number of elements the interface currently draws (diagnostics).</summary>
		public static int ElementCount => CountElements(layoutCanvas) + CountElements(immediateCanvas);

		// -----------------------------------------------------------------
		// Implementation
		// -----------------------------------------------------------------

		private static bool EnsureRenderer()
		{
			if (uiRenderer == null)
			{
				uiRenderer = new UiRenderer();
			}
			return uiRenderer.EnsureReady();
		}

		private static void ReleaseLayoutCanvas()
		{
			// A canvas owns no device resource: its children only have to be let go
			// of, and the native font atlases they hold belong to the canvas' font
			// cache, which goes with it.
			layoutCanvas?.ClearChildren();
			layoutCanvas = null;
		}

		private static string ResolveLayoutFilePath(string layoutFileName)
		{
			if (Path.IsPathRooted(layoutFileName) || layoutFileName.IndexOf('/') >= 0 || layoutFileName.IndexOf('\\') >= 0)
			{
				return layoutFileName;
			}

			string fileName = layoutFileName.EndsWith(LayoutFileExtension, StringComparison.OrdinalIgnoreCase)
				? layoutFileName
				: layoutFileName + LayoutFileExtension;

			return Path.Combine(Application.ExecutableDirectory, LayoutDirectoryName, fileName);
		}

		private static int CountElements(UiElement? rootElement)
		{
			if (rootElement == null)
			{
				return 0;
			}

			int elementCount = 1;
			IReadOnlyList<UiElement> children = rootElement.Children;
			for (int childIndex = 0; childIndex < children.Count; ++childIndex)
			{
				elementCount += CountElements(children[childIndex]);
			}
			return elementCount;
		}
	}
}

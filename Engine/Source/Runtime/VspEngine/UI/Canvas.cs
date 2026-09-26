using System;
using System.Collections.Generic;
using System.Numerics;

namespace VspEngine.UI
{
	/// <summary>
	/// The root of a UI tree and the thing a render pipeline talks to.
	///
	/// A canvas owns the theme, the fonts the tree draws with and the size of the
	/// render target, and it drives one frame in two calls:
	///
	///     canvas.Update(UiInputState.Capture(), width, height);   // layout + input
	///     canvas.BuildDrawList(drawList, width, height);          // quads
	///
	/// Fonts are cached PER PIXEL SIZE: a text element asks the canvas for the
	/// size it wants and gets a face that was rasterized at exactly that size, so
	/// a title and a caption do not have to share one blurry atlas.
	///
	/// The canvas itself draws nothing: it is the backdrop, the coordinate
	/// system and the font registry.
	/// </summary>
	public sealed class Canvas : UiElement
	{
		private readonly Dictionary<int, Font?> fontsByPixelSize = new Dictionary<int, Font?>();

		public Canvas(UiTheme? theme = null)
		{
			Theme = theme ?? UiTheme.CreateDefault();
			Name = "Canvas";
			IsEnabled = true;
		}

		/// <summary>Colours and measurements every widget in this tree draws with.</summary>
		public UiTheme Theme { get; }

		/// <summary>
		/// Bindless slot of the 1x1 white texture solid quads sample; the UI
		/// renderer sets it, so a widget only has to ask for it.
		/// </summary>
		public int SolidTextureBindlessSlot { get; internal set; } = -1;

		/// <summary>The pixel size text elements use when they name none.</summary>
		public float DefaultTextPixelSize { get; set; } = 18.0f;

		/// <summary>Size of the canvas in pixels (the render target's size).</summary>
		public float Width { get; private set; }

		public float Height { get; private set; }

		/// <summary>
		/// The font rasterized at the given pixel size, loaded on first use and
		/// cached afterwards. Null when no font could be loaded at all, which
		/// text elements survive by drawing nothing.
		/// </summary>
		public Font? GetFont(float pixelSize)
		{
			// Sizes are cached per whole pixel: a layout that animates a size
			// would otherwise load a face per frame.
			int key = (int)MathF.Round(pixelSize, MidpointRounding.AwayFromZero);
			if (key <= 0)
			{
				key = 1;
			}

			if (fontsByPixelSize.TryGetValue(key, out Font? cachedFont))
			{
				return cachedFont;
			}

			Font? loadedFont = Font.LoadDefault(key);
			fontsByPixelSize[key] = loadedFont;
			return loadedFont;
		}

		/// <summary>
		/// Brings every font this canvas loaded in step with its native atlas and
		/// reports whether any of them changed. The UI renderer calls it before
		/// and after building a frame, so a glyph rasterized while the list was
		/// built reaches the texture in the same frame.
		/// </summary>
		public bool SyncFontAtlases()
		{
			bool didAnyAtlasChange = false;
			foreach (Font? font in fontsByPixelSize.Values)
			{
				if (font == null || !font.IsValid)
				{
					continue;
				}

				uint versionBefore = font.AtlasVersion;
				font.SyncAtlas();
				if (font.AtlasVersion != versionBefore)
				{
					didAnyAtlasChange = true;
				}
			}
			return didAnyAtlasChange;
		}

		/// <summary>Runs one frame of the tree: layout, hover state, widget reactions.</summary>
		public void Update(UiInputState input, float width, float height)
		{
			Width = width;
			Height = height;

			Bounds = new UiRect(0.0f, 0.0f, width, height);
			UpdateHierarchy(input, new UiRect(0.0f, 0.0f, width, height), Theme, isParentInteractive: true);
		}

		/// <summary>
		/// Appends the whole tree to the frame's draw list. The projection that
		/// carries pixel space into clip space comes from the engine's NATIVE
		/// matrix maths, so a UI quad is transformed by exactly the same code the
		/// rest of the engine uses.
		/// </summary>
		public void BuildDrawList(UiDrawList drawList, float width, float height)
		{
			Matrix4x4 projection = NativeMath.BuildOrthographicPixelSpace(width, height);
			DrawHierarchy(drawList, Theme, projection);
		}

		protected override void OnChildAdded(UiElement child)
		{
			AdoptCanvas(this, child);
		}
	}
}

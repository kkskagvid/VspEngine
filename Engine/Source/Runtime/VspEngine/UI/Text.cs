using System;
using System.Numerics;
using VspEngine.Rendering;

namespace VspEngine.UI
{
	/// <summary>Where a text run sits horizontally inside its rectangle.</summary>
	public enum UiTextAlignment
	{
		Left = 0,
		Center = 1,
		Right = 2,
	}

	/// <summary>Where a text run sits vertically inside its rectangle.</summary>
	public enum UiTextVerticalAlignment
	{
		Top = 0,
		Middle = 1,
		Bottom = 2,
	}

	/// <summary>
	/// A run of text.
	///
	/// The string comes from the engine's localisation service: set
	/// <see cref="TranslationKey"/> and the text is whatever
	/// <c>I18N.Translate</c> resolves for the current locale - the point of a key
	/// is that the same UI reads correctly in every language, and that a missing
	/// translation shows the key instead of an empty label. Set
	/// <see cref="Literal"/> for text that must NOT be translated (a number, a
	/// player name, a debug readout).
	///
	/// Shaping and rasterization happen in the NATIVE text service
	/// (<c>Core/Text</c>): this element asks its font to lay the string out into
	/// glyph quads and pushes them into the frame's draw list. The layout is
	/// cached and only redone when the string, the size, the font or the
	/// alignment actually changed, so a static label costs one native call for
	/// the whole run of the game.
	/// </summary>
	public class Text : UiElement
	{
		private string cachedSourceText = string.Empty;
		private string cachedResolvedText = string.Empty;
		private float cachedPixelSize = 0.0f;
		private float cachedMeasuredWidth = 0.0f;
		private float cachedMeasuredHeight = 0.0f;

		public Text()
		{
			Name = "Text";
			Color = new Color(1.0f, 1.0f, 1.0f, 1.0f);
		}

		/// <summary>
		/// Key looked up in the current locale. Takes precedence over
		/// <see cref="Literal"/>; empty means "draw the literal".
		/// </summary>
		public string TranslationKey { get; set; } = string.Empty;

		/// <summary>Literal text, used when no translation key is set.</summary>
		public string Literal { get; set; } = string.Empty;

		/// <summary>Rasterization size in pixels; 0 uses the canvas default.</summary>
		public float PixelSize { get; set; }

		/// <summary>Font to draw with; null uses the canvas font for the size.</summary>
		public Font? Font { get; set; }

		public Color Color { get; set; }

		public UiTextAlignment Alignment { get; set; } = UiTextAlignment.Left;

		public UiTextVerticalAlignment VerticalAlignment { get; set; } = UiTextVerticalAlignment.Middle;

		/// <summary>Extra offset applied after alignment, in pixels.</summary>
		public Vector2 Offset { get; set; } = Vector2.Zero;

		/// <summary>Width of the last measured run, in pixels.</summary>
		public float MeasuredWidth => cachedMeasuredWidth;

		/// <summary>Height of the last measured run, in pixels.</summary>
		public float MeasuredHeight => cachedMeasuredHeight;

		/// <summary>
		/// The string this element currently shows: the translation of
		/// <see cref="TranslationKey"/> when one is set, the literal otherwise.
		/// </summary>
		public string ResolveText()
		{
			if (!string.IsNullOrEmpty(TranslationKey))
			{
				return I18N.Translate(TranslationKey);
			}
			return Literal ?? string.Empty;
		}

		/// <summary>The font a frame draws this run with, or null.</summary>
		public Font? ResolveFont()
		{
			if (Font != null && Font.IsValid)
			{
				return Font;
			}

			float pixelSize = PixelSize > 0.0f ? PixelSize : (OwningCanvas?.DefaultTextPixelSize ?? 16.0f);
			return OwningCanvas?.GetFont(pixelSize);
		}

		/// <summary>
		/// Size the run occupies, in pixels: measured once per distinct string and
		/// size, so a layout that positions text next to text pays for the
		/// measurement once.
		/// </summary>
		public Vector2 Measure()
		{
			Font? font = ResolveFont();
			if (font == null || !font.IsValid)
			{
				return Vector2.Zero;
			}

			RefreshMeasurement(font);
			return new Vector2(cachedMeasuredWidth, cachedMeasuredHeight);
		}

		protected override void OnDraw(UiDrawList drawList, UiTheme theme, Matrix4x4 projection)
		{
			Font? font = ResolveFont();
			if (font == null || !font.IsValid)
			{
				return;
			}

			RefreshMeasurement(font);
			if (cachedMeasuredWidth <= 0.0f || cachedMeasuredHeight <= 0.0f)
			{
				return;
			}

			Vector2 origin = AlignWithinBounds(font);
			origin += Offset;

			ReadOnlySpan<float> glyphQuads = font.Layout(cachedResolvedText);
			if (glyphQuads.IsEmpty)
			{
				return;
			}

			int textureBindlessSlot = font.AtlasBindlessSlot;
			if (textureBindlessSlot < 0)
			{
				// The atlas has not been uploaded yet; the renderer syncs it
				// before it builds the list, so this only happens on the very
				// first frame of a font.
				return;
			}

			drawList.AddGlyphs(glyphQuads, origin, UiTheme.ToVector(Color), textureBindlessSlot, projection);
		}

		/// <summary>Measures the run again when the string or the size changed.</summary>
		private void RefreshMeasurement(Font font)
		{
			string sourceText = !string.IsNullOrEmpty(TranslationKey) ? TranslationKey : (Literal ?? string.Empty);
			float pixelSize = PixelSize > 0.0f ? PixelSize : (OwningCanvas?.DefaultTextPixelSize ?? 16.0f);

			if (sourceText == cachedSourceText && pixelSize == cachedPixelSize)
			{
				return;
			}

			cachedSourceText = sourceText;
			cachedPixelSize = pixelSize;
			cachedResolvedText = ResolveText();

			if (!font.Measure(cachedResolvedText, out float width, out float height))
			{
				cachedMeasuredWidth = 0.0f;
				cachedMeasuredHeight = 0.0f;
				return;
			}

			cachedMeasuredWidth = width;
			cachedMeasuredHeight = height;
		}

		/// <summary>
		/// Where the run's text origin sits, from the alignment. The native layout
		/// measures from the pen position at the START OF THE FIRST LINE, which is
		/// the first baseline - glyph tops are negative relative to it - so the
		/// font's ascent is what turns "the top of the box" into "the origin the
		/// layout expects".
		/// </summary>
		private Vector2 AlignWithinBounds(Font font)
		{
			UiRect bounds = AbsoluteBounds;

			float x = bounds.Left;
			if (Alignment == UiTextAlignment.Center)
			{
				x = bounds.Left + ((bounds.Width - cachedMeasuredWidth) * 0.5f);
			}
			else if (Alignment == UiTextAlignment.Right)
			{
				x = bounds.Right - cachedMeasuredWidth;
			}

			float y = bounds.Top;
			if (VerticalAlignment == UiTextVerticalAlignment.Middle)
			{
				y = bounds.Top + ((bounds.Height - cachedMeasuredHeight) * 0.5f);
			}
			else if (VerticalAlignment == UiTextVerticalAlignment.Bottom)
			{
				y = bounds.Bottom - cachedMeasuredHeight;
			}

			return new Vector2(x, y + font.Ascent);
		}
	}
}

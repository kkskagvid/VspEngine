using System;
using VspEngine.Rendering;

namespace VspEngine.UI
{
	/// <summary>
	/// A loaded font: the managed handle of a face the NATIVE text service owns
	/// (<c>Core/Text</c> in VspCore), plus the coverage atlas that face produces
	/// as a <see cref="Texture2D"/> a UI shader can sample.
	///
	/// Face, shaping and rasterization all live natively: this class only carries
	/// the handle, caches the layout buffer a text run is written into and keeps
	/// the atlas upload in step. The atlas grows as new glyphs are needed, which
	/// is what <see cref="SyncAtlas"/> detects through the native atlas version
	/// and turns into a fresh texture - the old one is released, so its bindless
	/// slot comes back.
	///
	/// The managed side never rasterizes anything itself, and never throws.
	/// </summary>
	public sealed class Font : IDisposable
	{
		/// <summary>Floats one glyph quad occupies in the native layout buffer.</summary>
		private const int QuadFloatCount = 8;

		private readonly float[] measureScratch = new float[2];
		private float[] quadBuffer = new float[QuadFloatCount * 64];
		private byte[] atlasPixels = Array.Empty<byte>();
		private uint atlasVersion = uint.MaxValue;

		private uint nativeHandle;

		private Font(uint handle)
		{
			nativeHandle = handle;
		}

		/// <summary>Native font handle (0 = invalid).</summary>
		public uint NativeHandle => nativeHandle;

		public bool IsValid => nativeHandle != 0 && TextApi.VspText_IsFontValid(nativeHandle) != 0;

		/// <summary>Line height in pixels at this font's rasterization size.</summary>
		public float LineHeight => IsValid ? TextApi.VspText_GetLineHeight(nativeHandle) : 0.0f;

		/// <summary>Distance from the text origin down to the baseline, in pixels.</summary>
		public float Ascent => IsValid ? TextApi.VspText_GetAscent(nativeHandle) : 0.0f;

		/// <summary>Distance from the baseline down to the next line, in pixels.</summary>
		public float Descent => IsValid ? TextApi.VspText_GetDescent(nativeHandle) : 0.0f;

		/// <summary>
		/// The atlas as a texture, or null while the font is invalid. The
		/// renderer calls <see cref="SyncAtlas"/> once per frame before it draws,
		/// which is what keeps this current.
		/// </summary>
		public Texture2D? Atlas { get; private set; }

		/// <summary>Bindless slot of the atlas (-1 while there is none).</summary>
		public int AtlasBindlessSlot => Atlas?.BindlessSlot ?? -1;

		/// <summary>
		/// Version of the atlas the current <see cref="Atlas"/> texture was built
		/// from. <see cref="SyncAtlas"/> advances it, which is how a caller knows
		/// an upload actually happened.
		/// </summary>
		public uint AtlasVersion => atlasVersion;

		/// <summary>
		/// The interface font the engine picks for the given pixel size: the
		/// first font file in the executable's Fonts folder, and a well-known
		/// system font when that folder holds none. Null when no font could be
		/// loaded, which a UI has to survive (it then draws no text).
		/// </summary>
		public static Font? LoadDefault(float pixelSize)
		{
			uint handle = TextApi.VspText_CreateDefaultFont(pixelSize);
			if (handle == 0)
			{
				Debug.LogWarning("Font: no interface font could be loaded; text will not be drawn.");
				return null;
			}
			return new Font(handle);
		}

		/// <summary>Loads one TTF/OTF file at the given pixel size.</summary>
		public static Font? Load(string filePath, float pixelSize)
		{
			if (string.IsNullOrEmpty(filePath))
			{
				Debug.LogError("Font: Load was given an empty path.");
				return null;
			}

			uint handle = TextApi.VspText_CreateFont(filePath, pixelSize);
			return handle != 0 ? new Font(handle) : null;
		}

		/// <summary>Measures a run; the width and height are in pixels.</summary>
		public bool Measure(string text, out float width, out float height)
		{
			width = 0.0f;
			height = 0.0f;
			if (!IsValid || string.IsNullOrEmpty(text))
			{
				return false;
			}

			if (TextApi.VspText_MeasureText(nativeHandle, text, measureScratch) == 0)
			{
				return false;
			}

			width = measureScratch[0];
			height = measureScratch[1];
			return true;
		}

		/// <summary>
		/// Shapes a run into glyph quads in TEXT PIXEL SPACE (origin at the pen
		/// position, +X right, +Y down). The returned span is only valid until
		/// the next call on this font; the caller copies what it needs.
		/// </summary>
		public ReadOnlySpan<float> Layout(string text)
		{
			if (!IsValid || string.IsNullOrEmpty(text))
			{
				return ReadOnlySpan<float>.Empty;
			}

			uint requiredQuadCount = TextApi.VspText_LayoutText(nativeHandle, text, quadBuffer, (uint)(quadBuffer.Length / QuadFloatCount));

			// A longer run than the buffer holds: grow once and ask again. This
			// happens on the first long string only, so the steady state costs
			// exactly one native call per text element and frame.
			if (requiredQuadCount * QuadFloatCount > quadBuffer.Length)
			{
				quadBuffer = new float[requiredQuadCount * QuadFloatCount];
				requiredQuadCount = TextApi.VspText_LayoutText(nativeHandle, text, quadBuffer, requiredQuadCount);
			}

			return new ReadOnlySpan<float>(quadBuffer, 0, (int)(requiredQuadCount * QuadFloatCount));
		}

		/// <summary>
		/// Brings <see cref="Atlas"/> in step with the native font: the native
		/// atlas version changes whenever a glyph was rasterized into it, and the
		/// texture is then built again from the coverage pixels. Recreating the
		/// texture releases the previous bindless slot through Dispose.
		/// </summary>
		public void SyncAtlas()
		{
			if (!IsValid)
			{
				return;
			}

			uint currentVersion = TextApi.VspText_GetAtlasVersion(nativeHandle);
			if (Atlas != null && Atlas.IsValid && currentVersion == atlasVersion)
			{
				return;
			}

			int width = TextApi.VspText_GetAtlasWidth(nativeHandle);
			int height = TextApi.VspText_GetAtlasHeight(nativeHandle);
			if (width <= 0 || height <= 0)
			{
				return;
			}

			int requiredByteCount = width * height * 4;
			if (atlasPixels.Length != requiredByteCount)
			{
				atlasPixels = new byte[requiredByteCount];
			}

			int copiedByteCount = TextApi.VspText_CopyAtlasPixels(nativeHandle, atlasPixels, (uint)atlasPixels.Length);
			if (copiedByteCount != requiredByteCount)
			{
				Debug.LogError("Font: the atlas copy returned " + copiedByteCount + " of " + requiredByteCount + " byte(s).");
				return;
			}

			Texture2D? newAtlas = new Texture2D((uint)width, (uint)height, atlasPixels);
			if (newAtlas == null || !newAtlas.IsValid)
			{
				newAtlas?.Dispose();
				Debug.LogError("Font: the atlas texture could not be created.");
				return;
			}

			Atlas?.Dispose();
			Atlas = newAtlas;
			atlasVersion = currentVersion;
		}

		public void Dispose()
		{
			Atlas?.Dispose();
			Atlas = null;

			if (nativeHandle != 0)
			{
				TextApi.VspText_DestroyFont(nativeHandle);
				nativeHandle = 0;
			}
		}
	}
}

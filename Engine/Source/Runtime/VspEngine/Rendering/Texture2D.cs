using System;

namespace VspEngine.Rendering
{
	/// <summary>
	/// An RGBA8 texture. The native backend stores it in the bindless sampled
	/// image array and reports the slot it landed in: shaders index that array
	/// with the value, which the pipeline writes into its push constants.
	/// </summary>
	public sealed class Texture2D : IDisposable
	{
		private uint nativeHandle;

		/// <summary>Creates a texture from tightly packed RGBA8 pixels.</summary>
		public Texture2D(uint width, uint height, byte[] rgbaPixels)
		{
			if (width == 0 || height == 0)
			{
				throw new ArgumentOutOfRangeException(nameof(width), "A texture needs non-zero dimensions.");
			}
			if (rgbaPixels == null || rgbaPixels.Length < (long)width * height * 4)
			{
				throw new ArgumentException("The pixel buffer must hold width * height * 4 RGBA bytes.", nameof(rgbaPixels));
			}

			nativeHandle = RhiApi.VspRhi_CreateTexture(width, height, rgbaPixels);
			if (nativeHandle != 0)
			{
				BindlessSlot = RhiApi.VspRhi_GetTextureBindlessSlot(nativeHandle);
			}
		}

		/// <summary>Native texture handle (0 = invalid).</summary>
		public uint NativeHandle => nativeHandle;

		public bool IsValid => nativeHandle != 0;

		/// <summary>Slot of this texture in the bindless sampled image array (-1 = invalid).</summary>
		public int BindlessSlot { get; } = -1;

		/// <summary>Creates a fully white texture, the neutral default for textured draws.</summary>
		public static Texture2D? CreateWhite(uint width, uint height)
		{
			byte[] pixels = new byte[width * height * 4];
			for (int byteIndex = 0; byteIndex < pixels.Length; ++byteIndex)
			{
				pixels[byteIndex] = 255;
			}
			return new Texture2D(width, height, pixels);
		}

		public void Dispose()
		{
			if (nativeHandle != 0)
			{
				RhiApi.VspRhi_DestroyTexture(nativeHandle);
				nativeHandle = 0;
			}
		}
	}
}

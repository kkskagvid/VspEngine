using System;
using System.Runtime.InteropServices;

namespace VspEngine.UI
{
	/// <summary>
	/// Raw P/Invoke bindings of the native text service (VspText_* in VspCore).
	/// <see cref="Font"/> is the managed facade meant to be used.
	///
	/// Every function takes the 1-based font handle the native
	/// <c>Core/Text/TextSystem</c> handed out; 0 always means "invalid", and the
	/// native side logs the reason before returning an empty result.
	/// </summary>
	internal static class TextApi
	{
		private const string LibraryName = "VspCore";

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspText_CreateFont([MarshalAs(UnmanagedType.LPUTF8Str)] string filePathUtf8, float pixelSize);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspText_CreateDefaultFont(float pixelSize);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspText_DestroyFont(uint fontHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspText_IsFontValid(uint fontHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspText_GetLineHeight(uint fontHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspText_GetAscent(uint fontHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspText_GetDescent(uint fontHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspText_MeasureText(uint fontHandle, [MarshalAs(UnmanagedType.LPUTF8Str)] string textUtf8, [Out] float[] outSize2);

		/// <summary>
		/// Lays a run out into 8 floats per glyph quad - left, top, right,
		/// bottom, u0, v0, u1, v1 - and returns the TOTAL number of quads the run
		/// needs, which may exceed <paramref name="maxQuadCount"/>.
		/// </summary>
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspText_LayoutText(uint fontHandle, [MarshalAs(UnmanagedType.LPUTF8Str)] string textUtf8, [Out] float[] outQuads, uint maxQuadCount);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspText_GetAtlasWidth(uint fontHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspText_GetAtlasHeight(uint fontHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern uint VspText_GetAtlasVersion(uint fontHandle);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspText_CopyAtlasPixels(uint fontHandle, [Out] byte[] outPixelsRgba8, uint bufferCapacityBytes);
	}
}

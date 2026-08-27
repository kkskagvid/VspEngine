using System;
using System.Runtime.InteropServices;

namespace VspEngine
{
	/// <summary>
	/// Raw P/Invoke bindings against the native engine runtime (VspRuntime.dll).
	/// These functions form the C# -> C++ direction of the interop bridge.
	/// The C++ host calls back into managed code through NativeBridge.
	/// </summary>
	internal static class NativeApi
	{
		private const string LibraryName = "VspRuntime";

		// ---- Input (keyboard) ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspInput_IsKeyDown(int keyCode);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspInput_WasKeyPressed(int keyCode);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspInput_WasKeyReleased(int keyCode);

		// ---- Input (mouse) ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspInput_IsMouseButtonDown(int button);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspInput_WasMouseButtonPressed(int button);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspInput_WasMouseButtonReleased(int button);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspInput_GetMousePositionX();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspInput_GetMousePositionY();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspInput_GetMouseDeltaX();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspInput_GetMouseDeltaY();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspInput_GetScrollX();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspInput_GetScrollY();

		// ---- Time ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspTime_GetDeltaTime();

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspTime_GetElapsedTime();

		// ---- Transform ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspTransform_SetPosition(uint instanceId, float x, float y);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspTransform_GetPositionX(uint instanceId);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern float VspTransform_GetPositionY(uint instanceId);

		// ---- Renderer ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspRenderer_SetColorMode(uint instanceId, int colorMode);

		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern int VspRenderer_GetColorMode(uint instanceId);

		// ---- Logging ----
		[DllImport(LibraryName, CallingConvention = CallingConvention.Cdecl)]
		internal static extern void VspLog_Message([MarshalAs(UnmanagedType.LPUTF8Str)] string message);
	}
}

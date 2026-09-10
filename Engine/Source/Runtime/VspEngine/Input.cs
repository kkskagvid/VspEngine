using System.Numerics;

namespace VspEngine
{
	/// <summary>Unity-style polling input facade. State is owned by the native engine.</summary>
	public static class Input
	{
		/// <summary>True while the key is held down.</summary>
		public static bool GetKey(KeyCode key) => NativeApi.VspInput_IsKeyDown((int)key) != 0;

		/// <summary>True only on the frame the key went down.</summary>
		public static bool GetKeyDown(KeyCode key) => NativeApi.VspInput_WasKeyPressed((int)key) != 0;

		/// <summary>True only on the frame the key was released.</summary>
		public static bool GetKeyUp(KeyCode key) => NativeApi.VspInput_WasKeyReleased((int)key) != 0;

		/// <summary>True while the mouse button (0 = left, 1 = right, 2 = middle) is held down.</summary>
		public static bool GetMouseButton(int button) => NativeApi.VspInput_IsMouseButtonDown(button) != 0;

		/// <summary>True only on the frame the mouse button went down.</summary>
		public static bool GetMouseButtonDown(int button) => NativeApi.VspInput_WasMouseButtonPressed(button) != 0;

		/// <summary>True only on the frame the mouse button was released.</summary>
		public static bool GetMouseButtonUp(int button) => NativeApi.VspInput_WasMouseButtonReleased(button) != 0;

		/// <summary>Mouse cursor position in window client coordinates.</summary>
		public static Vector2 MousePosition =>
			new Vector2(NativeApi.VspInput_GetMousePositionX(), NativeApi.VspInput_GetMousePositionY());

		/// <summary>Mouse movement between the previous and the current frame.</summary>
		public static Vector2 MouseDelta =>
			new Vector2(NativeApi.VspInput_GetMouseDeltaX(), NativeApi.VspInput_GetMouseDeltaY());

		/// <summary>Mouse wheel scroll amount accumulated this frame.</summary>
		public static Vector2 MouseScroll =>
			new Vector2(NativeApi.VspInput_GetScrollX(), NativeApi.VspInput_GetScrollY());
	}
}

namespace VspEngine
{
	/// <summary>How the mouse pointer behaves while the game is played.</summary>
	public enum CursorMode
	{
		/// <summary>The ordinary pointer: visible, and free to leave the window.</summary>
		Visible = 0,

		/// <summary>Visible, but held inside the window's client area.</summary>
		Confined = 1,

		/// <summary>
		/// Hidden, and put back at the centre of the window after every frame. This
		/// is the mode a camera the mouse turns uses: the pointer keeps reporting
		/// how far the hand moved without ever reaching an edge, so the view can
		/// keep turning in one movement for as long as the player keeps moving.
		/// </summary>
		Locked = 2,
	}

	/// <summary>
	/// The mouse pointer, as a game reaches it.
	///
	/// Hiding the pointer is only half of a mouse look: a pointer that is merely
	/// hidden still walks off the window and stops producing movement. In
	/// <see cref="CursorMode.Locked"/> the engine hides it, holds it inside the
	/// window and puts it back at the centre after every frame, so the movement a
	/// script reads from <see cref="Input.MouseDelta"/> is a movement of the HAND
	/// and nothing else. The engine recentres the pointer itself, after the frame
	/// has read it, so no script ever sees the jump.
	///
	/// The engine gives the pointer back when the game ends, and a mode that is
	/// never asked for leaves it exactly as the operating system had it.
	/// </summary>
	public static class Cursor
	{
		/// <summary>
		/// How the pointer behaves. Setting it takes effect on the next frame, and
		/// the engine keeps asking for it every frame, so a window that was in the
		/// background catches up as soon as the player returns to it.
		/// </summary>
		public static CursorMode Mode
		{
			get => (CursorMode)NativeApi.VspInput_GetCursorMode();
			set => NativeApi.VspInput_SetCursorMode((int)value);
		}

		/// <summary>
		/// True while the pointer is hidden and recentred every frame, which is the
		/// state an interface that draws its own pointer has to know about.
		/// </summary>
		public static bool IsLocked => NativeApi.VspInput_IsCursorLocked() != 0;
	}
}

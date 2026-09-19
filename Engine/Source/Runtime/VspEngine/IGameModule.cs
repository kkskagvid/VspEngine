namespace VspEngine
{
	/// <summary>
	/// Entry point of a game assembly. The engine hosts one game assembly
	/// (Assembly.dll) and, right after loading it, calls <see cref="OnGameLoad"/>
	/// on the single type that implements this interface.
	///
	/// This is where a game takes over: it installs the render pipeline it wants
	/// (<see cref="Rendering.RenderPipelineManager.ActivePipeline"/>) and loads
	/// the shaders that pipeline draws with (<see cref="Shader.Load"/>). The
	/// engine installs no pipeline and loads no shader of its own, so nothing is
	/// rendered - and no shader asset is read from disk - until a game says so.
	/// </summary>
	public interface IGameModule
	{
		/// <summary>
		/// Called once, after the game assembly loaded and before the first
		/// frame. Throwing is contained by the host and reported in the log.
		/// </summary>
		void OnGameLoad();

		/// <summary>
		/// Called once before the engine shuts down, while the graphics backend
		/// is still alive, so the game can release what it created.
		/// </summary>
		void OnGameUnload();
	}
}

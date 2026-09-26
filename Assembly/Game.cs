using Assembly.Rendering;

using VspEngine;
using VspEngine.Rendering;

namespace Assembly
{
	/// <summary>
	/// Entry point of the game assembly. The engine calls <see cref="OnGameLoad"/>
	/// once, right after it loaded this assembly, and that is where the game
	/// takes over rendering: it installs its own pipeline.
	///
	/// The pipeline is what loads the game's shaders (<see cref="Shader.Load"/>),
	/// which is why nothing is drawn - and no shader file is read - before this
	/// runs. The engine ships no pipeline and loads no shader by itself.
	/// </summary>
	public sealed class Game : IGameModule
	{
		private LitCubeRenderPipeline? pipeline;

		public void OnGameLoad()
		{
			pipeline = new LitCubeRenderPipeline();
			RenderPipelineManager.ActivePipeline = pipeline;

			Debug.Log("Game: installed " + nameof(LitCubeRenderPipeline)
				+ " (shader '" + LitCubeRenderPipeline.ShaderName + "' is loaded on its first frame).");
		}

		public void OnGameUnload()
		{
			// Releasing the pipeline releases the graphics resources it created
			// (the shader modules live in the pipelines, so they go with it).
			RenderPipelineManager.ActivePipeline = null;
			pipeline = null;
		}
	}
}

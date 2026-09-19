using System;
using System.Collections.Generic;
using System.Numerics;

namespace VspEngine.Rendering
{
	/// <summary>
	/// Drives the programmable render pipeline. The native host calls
	/// <see cref="Render"/> once per frame, right after the scripts updated:
	/// the manager gathers the frame's draw list from the native scene, opens a
	/// frame in the wrapped graphics API, hands the context to the active
	/// pipeline and makes sure the frame gets closed.
	///
	/// The engine ships NO pipeline of its own: rendering, and with it every
	/// shader load, belongs to the game assembly, which installs its pipeline
	/// through <see cref="ActivePipeline"/> or
	/// <see cref="SetDefaultPipelineFactory"/>. Until a game installs one, a
	/// frame is opened and closed without any draw command, which leaves the
	/// window cleared.
	/// </summary>
	public static class RenderPipelineManager
	{
		private static readonly List<RenderDrawItem> frameDrawItems = new List<RenderDrawItem>();
		private static readonly ScriptableRenderContext renderContext = new ScriptableRenderContext();

		// Scratch buffer the native transform getter fills with x, y, z; kept
		// here so gathering a frame allocates nothing.
		private static readonly float[] positionXyzScratch = new float[3];

		private static RenderPipeline? activePipeline;
		private static Func<RenderPipeline>? defaultPipelineFactory;

		/// <summary>
		/// The pipeline the next frame runs; null until a game installs one. The
		/// getter builds the pipeline the game registered as its default, so a
		/// game can keep a fresh pipeline per graphics device.
		/// </summary>
		public static RenderPipeline? ActivePipeline
		{
			get => activePipeline ??= defaultPipelineFactory?.Invoke();
			set
			{
				if (!ReferenceEquals(activePipeline, value))
				{
					activePipeline?.Dispose();
					activePipeline = value;
				}
			}
		}

		/// <summary>
		/// Sets the factory used while no pipeline was installed explicitly. A game
		/// assembly calls this during startup to install its own default; the
		/// engine never installs one by itself.
		/// </summary>
		public static void SetDefaultPipelineFactory(Func<RenderPipeline>? pipelineFactory)
		{
			defaultPipelineFactory = pipelineFactory;
			if (activePipeline != null)
			{
				activePipeline.Dispose();
				activePipeline = null;
			}
		}

		/// <summary>
		/// Runs one frame: gather the draw list, open the frame, let the active
		/// pipeline record it and close it if the pipeline did not.
		/// </summary>
		public static void Render()
		{
			GatherDrawItems();
			renderContext.BeginFrame(frameDrawItems, RhiApi.VspRhi_GetBackbufferWidth(), RhiApi.VspRhi_GetBackbufferHeight());

			// Every frame is a fresh command list in the wrapped graphics API.
			RhiApi.VspRhi_BeginFrame();

			RenderPipeline? pipeline = ActivePipeline;
			if (pipeline != null)
			{
				try
				{
					pipeline.Render(renderContext);
				}
				catch (Exception exception)
				{
					// A broken pipeline must not tear the process down: report the
					// failure and let the frame close with whatever was recorded.
					Debug.LogError("RenderPipeline: " + exception.Message);
				}
			}

			// Without a pipeline (or after one failed) the frame closes empty and
			// the window keeps showing the cleared background.
			renderContext.Submit();
		}

		/// <summary>
		/// Releases the active pipeline and its graphics resources. The engine
		/// calls it while the graphics backend is still alive.
		/// </summary>
		public static void ReleaseActivePipeline()
		{
			activePipeline?.Dispose();
			activePipeline = null;
			frameDrawItems.Clear();
		}

		/// <summary>
		/// Builds the frame's draw list from the native scene: every renderable
		/// component of an active game object contributes one entry.
		/// </summary>
		private static void GatherDrawItems()
		{
			frameDrawItems.Clear();

			uint renderableCount = NativeApi.VspComponent_GetRenderableCount();
			for (uint renderableIndex = 0; renderableIndex < renderableCount; ++renderableIndex)
			{
				uint componentHandle = NativeApi.VspComponent_GetRenderableHandle(renderableIndex);
				if (componentHandle == 0)
				{
					continue;
				}

				uint gameObjectHandle = NativeApi.VspComponent_GetOwnerGameObject(componentHandle);
				uint transformHandle = NativeApi.VspGameObject_GetTransform(gameObjectHandle);
				if (transformHandle == 0)
				{
					continue;
				}

				NativeApi.VspTransform_GetLocalPosition(transformHandle, positionXyzScratch);
				Vector3 position = new Vector3(positionXyzScratch[0], positionXyzScratch[1], positionXyzScratch[2]);

				uint materialHandle = NativeApi.VspComponent_GetMaterial(componentHandle);
				frameDrawItems.Add(new RenderDrawItem(componentHandle, transformHandle, position, materialHandle));
			}
		}
	}
}

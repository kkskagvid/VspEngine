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

		// Scratch buffers the native transform getters fill; kept here so
		// gathering a frame allocates nothing.
		private static readonly float[] positionXyzScratch = new float[3];
		private static readonly float[] worldMatrixScratch = new float[16];

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
		/// Runs one frame. The ENGINE frames it - it gathers the draw list, hands the
		/// frame its camera, opens its render pass and paints the sky into it - and the
		/// active pipeline records its own passes inside that frame. A frame nobody
		/// installed a pipeline for is still a frame: it shows the engine's sky.
		/// </summary>
		public static void Render()
		{
			GatherDrawItems();
			renderContext.BeginFrame(frameDrawItems, RhiApi.VspRhi_GetBackbufferWidth(), RhiApi.VspRhi_GetBackbufferHeight());

			RenderPipeline? pipeline = ActivePipeline;
			CommandBuffer commandBuffer = renderContext.CommandBuffer;

			// The camera the frame is looked at with. Asking a pipeline for it is what
			// lets it build its resources before its first pass runs, so the very first
			// frame already has a camera and a sky.
			Camera? frameCamera = pipeline?.Camera;
			if (frameCamera != null)
			{
				commandBuffer.SetCamera(frameCamera);
			}

			// The frame's render pass belongs to the frame, not to a pass of the game:
			// it is opened here, cleared to the colour the game set, and the engine's
			// sky is painted into it before the first thing the game draws - so the sky
			// is behind everything, whatever the pipeline does.
			commandBuffer.BeginRenderPass(RenderSettings.BackgroundColor);
			if (frameCamera != null)
			{
				SkyboxRenderer.DrawSky(commandBuffer);
			}

			if (pipeline != null)
			{
				try
				{
					pipeline.Render(renderContext);
				}
				catch (Exception exception)
				{
					// A broken pipeline must not tear the process down: report the
					// failure and close the frame with whatever was recorded.
					Debug.LogError("RenderPipeline: " + exception.Message);
				}
			}

			// ... and it is closed here, so a pass that forgot to is still inside a
			// complete frame rather than one the backend refuses.
			commandBuffer.EndRenderPass();

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
			SkyboxRenderer.ReleaseResources();
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

				// The world matrix is what a vertex stage needs; the position is
				// the same information in the form a 2D-style pipeline reads.
				NativeApi.VspTransform_GetWorldMatrix(transformHandle, worldMatrixScratch);
				NativeApi.VspTransform_GetWorldPosition(transformHandle, positionXyzScratch);
				Vector3 position = new Vector3(positionXyzScratch[0], positionXyzScratch[1], positionXyzScratch[2]);

				uint materialHandle = NativeApi.VspComponent_GetMaterial(componentHandle);
				frameDrawItems.Add(new RenderDrawItem(
					componentHandle, transformHandle, position, ToMatrix4x4(worldMatrixScratch), materialHandle));
			}
		}

		/// <summary>
		/// Turns the native column-major matrix into a <see cref="Matrix4x4"/>,
		/// which stores rows: element (row, column) sits at column * 4 + row.
		/// </summary>
		private static Matrix4x4 ToMatrix4x4(float[] columnMajorValues)
		{
			return new Matrix4x4(
				columnMajorValues[0], columnMajorValues[4], columnMajorValues[8], columnMajorValues[12],
				columnMajorValues[1], columnMajorValues[5], columnMajorValues[9], columnMajorValues[13],
				columnMajorValues[2], columnMajorValues[6], columnMajorValues[10], columnMajorValues[14],
				columnMajorValues[3], columnMajorValues[7], columnMajorValues[11], columnMajorValues[15]);
		}
	}
}

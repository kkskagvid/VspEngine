using System;
using System.Collections.Generic;
using System.Text;

namespace VspEngine.Rendering
{
	/// <summary>
	/// A render graph: the frame's passes, the transient textures they read and
	/// write, the order they have to run in and the lifetimes of the resources
	/// they exchange. A pipeline builds one graph per frame, compiles it and
	/// executes it, instead of hand-ordering passes and hoping the order is right.
	///
	/// The graph is honest about the backend it sits on. The wrapped graphics API
	/// can render into the swapchain back buffer only and can create sampled
	/// bindless textures only, so the graph never allocates a render target: a
	/// back-buffer pass is a root of the graph, a texture resource is bookkeeping
	/// for a texture the game already created, and the passes themselves do the
	/// render-pass begin and end, the state and the draws through
	/// <see cref="CommandBuffer"/>. What the graph adds is the part that is hard to
	/// keep in your head: which pass still matters, in what order the rest have to
	/// run, and how long each resource has to stay alive.
	/// </summary>
	/// <remarks>
	/// <para>
	/// A frame looks like this - build, compile, execute:
	/// </para>
	/// <code>
	/// private readonly RenderGraph graph = new RenderGraph("Main");
	///
	/// public override void Render(ScriptableRenderContext context)
	/// {
	///     graph.Reset();
	///
	///     RenderGraphTextureHandle shadowMap = graph.CreateTexture("ShadowMap", 1024, 1024, shadowTexture.BindlessSlot);
	///
	///     graph.AddPass("Shadow")
	///         .SetKind(RenderGraphPassKind.Compute)
	///         .WriteTexture(shadowMap)
	///         .SetExecute(passContext =&gt; RecordShadowPass(passContext, shadowMap))
	///         .Done();
	///
	///     graph.AddPass("Opaque")
	///         .SetKind(RenderGraphPassKind.Raster)
	///         .ReadTexture(shadowMap)
	///         .WriteBackBuffer()
	///         .SetExecute(passContext =&gt; RecordOpaquePass(passContext, shadowMap))
	///         .Done();
	///
	///     if (graph.Compile())
	///     {
	///         graph.Execute(context);
	///     }
	/// }
	///
	/// private static void RecordOpaquePass(RenderGraphContext passContext, RenderGraphTextureHandle shadowMap)
	/// {
	///     int shadowSlot = passContext.GetTextureBindlessSlot(shadowMap);
	///     passContext.CommandBuffer.BeginRenderPass(Color.Black);
	///     // ... bind the pipeline, push the slot, draw ...
	///     passContext.CommandBuffer.EndRenderPass();
	/// }
	/// </code>
	/// <para>
	/// What <see cref="Compile"/> does, in order:
	/// </para>
	/// <list type="number">
	/// <item><description>
	/// A graph with no pass rendering into the back buffer is rejected: nothing
	/// would reach the screen, so the failure is an error in the log and the
	/// method returns false.
	/// </description></item>
	/// <item><description>
	/// Every pass that does not contribute to a back-buffer pass is culled. The
	/// graph walks the read/write edges backwards from the back-buffer passes, so
	/// a pass survives only when something it produces is read on a path that
	/// ends at the swapchain. Only the last writer of a texture produces the value
	/// a reader sees, so a write the graph can prove is overwritten keeps nothing
	/// alive, and a pass that reads and writes one texture - an in-place effect -
	/// waits for the pass that filled it instead of for itself.
	/// </description></item>
	/// <item><description>
	/// The survivors are sorted so that a pass runs after everything that
	/// produces what it reads. The sort is stable - independent passes keep the
	/// order they were added in, which is what makes a graph readable and a frame
	/// reproducible - and a cycle is reported instead of hanging the frame.
	/// </description></item>
	/// <item><description>
	/// Each resource's first and last use is recorded as its lifetime, and the
	/// peak number of resources alive at once is counted.
	/// </description></item>
	/// </list>
	/// <para>
	/// What the graph deliberately does NOT do: it does not allocate or destroy
	/// anything, it does not begin or end the frame's render pass, it does not
	/// remove a duplicated render pass, and it does not reorder two passes that
	/// both write the same texture into one. Those need backend features the
	/// engine does not have yet - offscreen targets, aliasing, pass merging.
	/// </para>
	/// <para>
	/// Every failure in the render path is reported through <see cref="Debug"/> and
	/// turns into a false or empty result: <see cref="Compile"/> returns false,
	/// <see cref="Execute"/> does nothing, a pass callback that throws is logged
	/// and skipped while the rest of the frame still records. Nothing here throws,
	/// because a graph that cannot be built must cost one frame, not the process.
	/// </para>
	/// </remarks>
	public sealed class RenderGraph
	{
		/// <summary>Prefix of every message this graph writes into the engine log.</summary>
		public const string LogTag = "RenderGraph";

		// The two swapchain resources are created first and always exist, so
		// their indices are fixed and the hot paths can name them directly.
		private const int BackBufferColorResourceIndex = 0;
		private const int BackBufferDepthResourceIndex = 1;

		private readonly string name;
		private readonly string logPrefix;
		private readonly List<RenderGraphResource> resources = new List<RenderGraphResource>();
		private readonly List<RenderGraphPass> passes = new List<RenderGraphPass>();
		private readonly List<int> executionOrder = new List<int>();

		// Scratch the compile step fills. They are fields rather than locals
		// because a graph is rebuilt every frame: after the first frame none of
		// this allocates, which is the same reason the pipeline manager keeps its
		// draw-list scratch around.
		private readonly List<int> pendingPassIndices = new List<int>();
		private readonly List<int> edgeSources = new List<int>();
		private readonly List<int> edgeTargets = new List<int>();
		private readonly StringBuilder summaryBuilder = new StringBuilder();
		private bool[] alivePasses = Array.Empty<bool>();
		private bool[] emittedPasses = Array.Empty<bool>();
		private int[] inDegrees = Array.Empty<int>();
		private int[] orderOfPass = Array.Empty<int>();
		private RenderGraphContext? executionContext;
		private bool isCompiled;
		private int culledPassCount;
		private int executedPassCount;
		private int peakLiveResourceCount;

		/// <summary>
		/// Creates an empty graph. The name is for the log and the debug summary;
		/// a graph with no name still works and is called "RenderGraph".
		/// </summary>
		public RenderGraph(string graphName)
		{
			name = string.IsNullOrEmpty(graphName) ? LogTag : graphName;
			logPrefix = LogTag + " '" + name + "'";
			CreateBackBufferResources();
		}

		/// <summary>Name of the graph, as it appears in the log and in the debug summary.</summary>
		public string Name => name;

		/// <summary>
		/// True between a <see cref="Compile"/> that succeeded and the next
		/// <see cref="Reset"/>. An uncompiled graph cannot be executed.
		/// </summary>
		public bool IsCompiled => isCompiled;

		/// <summary>
		/// Every pass the graph was given, culled and disabled ones included, in
		/// the order they were added. <see cref="ExecutionOrder"/> holds the order
		/// the survivors run in.
		/// </summary>
		public IReadOnlyList<RenderGraphPass> Passes => passes;

		/// <summary>
		/// Every resource the graph knows about, the two swapchain slices first.
		/// Culled passes do not extend a lifetime, so a resource only a culled pass
		/// touched stays unused.
		/// </summary>
		public IReadOnlyList<RenderGraphResource> Resources => resources;

		/// <summary>
		/// Indices into <see cref="Passes"/> in the order the surviving passes run,
		/// as the last <see cref="Compile"/> worked it out; empty while the graph
		/// is not compiled.
		/// </summary>
		public IReadOnlyList<int> ExecutionOrder => executionOrder;

		/// <summary>Handle of the swapchain color resource every back-buffer pass renders into.</summary>
		public RenderGraphTextureHandle BackBufferColorHandle => new RenderGraphTextureHandle(BackBufferColorResourceIndex);

		/// <summary>
		/// Handle of the depth slice of the frame's render pass. A pass declares a
		/// read or write of it when the depth it leaves behind (or expects) is what
		/// another pass depends on; <see cref="RenderGraphPassBuilder.WriteBackBuffer"/>
		/// claims the color slice only, because whether a pass writes depth is
		/// pipeline state the graph cannot see.
		/// </summary>
		public RenderGraphTextureHandle BackBufferDepthHandle => new RenderGraphTextureHandle(BackBufferDepthResourceIndex);

		/// <summary>
		/// Number of passes the last <see cref="Compile"/> removed from the frame,
		/// disabled passes included: neither a pass nothing reads nor a pass its
		/// author switched off can contribute to the back buffer.
		/// </summary>
		public int CulledPassCount => culledPassCount;

		/// <summary>
		/// Number of passes the last <see cref="Execute"/> ran. A pass with no
		/// callback counts - the graph walked it - and so does a pass whose
		/// callback threw, because the frame went on either way. Zero before the
		/// first execute.
		/// </summary>
		public int ExecutedPassCount => executedPassCount;

		/// <summary>
		/// Highest number of resources alive at one position of the execution
		/// order, over the whole frame: a resource is live from the pass that first
		/// uses it to the pass that uses it last, and every resource counts, the
		/// two swapchain slices included. It is the number a backend with real
		/// offscreen targets would need that many targets for - the figure the
		/// current backend cannot yet spend on aliasing.
		/// </summary>
		public int PeakLiveResourceCount => peakLiveResourceCount;

		/// <summary>
		/// Declares a texture resource: a texture the game already created, named
		/// here so passes can read and write it by handle. The graph copies the
		/// bindless slot so a pass can ask the context for it while it records, and
		/// keeps the size for the debug summary; it allocates nothing.
		///
		/// A declaration must happen between <see cref="Reset"/> and
		/// <see cref="Compile"/> - once the graph is compiled, authoring calls are
		/// reported and ignored. A non-positive size is reported and stored as
		/// "unknown", and a duplicated name is reported, because both are almost
		/// always a typo in the pipeline rather than an intent.
		/// </summary>
		public RenderGraphTextureHandle CreateTexture(string name, int width, int height, int bindlessSlot = -1)
		{
			if (isCompiled)
			{
				Debug.LogWarning(
					"{0}: CreateTexture('{1}') was ignored because the graph is compiled; call Reset() before authoring the next frame.",
					logPrefix, name);
				return RenderGraphTextureHandle.Invalid;
			}

			string resourceName = string.IsNullOrEmpty(name) ? "Texture" + resources.Count : name;
			if (FindResourceIndex(resourceName) >= 0)
			{
				Debug.LogWarning("{0}: a resource named '{1}' already exists in this graph.", logPrefix, resourceName);
			}

			if (width <= 0 || height <= 0)
			{
				Debug.LogWarning(
					"{0}: texture '{1}' was declared as {2}x{3}; the graph stores no pixels, so the size is recorded as unknown.",
					logPrefix, resourceName, width, height);
				width = 0;
				height = 0;
			}

			resources.Add(new RenderGraphResource(resourceName, RenderGraphResourceKind.Texture, width, height, bindlessSlot));
			return new RenderGraphTextureHandle(resources.Count - 1);
		}

		/// <summary>
		/// Adds a pass and returns the builder that configures it. The pass exists
		/// in the graph from here on, so a builder that is never finished still
		/// leaves a (probably culled) pass behind.
		///
		/// Like every authoring call this one is only valid while the graph is
		/// being built: after a successful <see cref="Compile"/> the builder handed
		/// back is invalid, its methods do nothing and the attempt is reported.
		/// </summary>
		public RenderGraphPassBuilder AddPass(string passName)
		{
			if (isCompiled)
			{
				Debug.LogWarning(
					"{0}: AddPass('{1}') was ignored because the graph is compiled; call Reset() before authoring the next frame.",
					logPrefix, passName);
				return default;
			}

			string resolvedName = string.IsNullOrEmpty(passName) ? "Pass" + passes.Count : passName;
			RenderGraphPass pass = new RenderGraphPass(passes.Count, resolvedName);
			passes.Add(pass);
			return new RenderGraphPassBuilder(this, pass.Index);
		}

		/// <summary>The pass a handle names, or null when the handle is invalid or belongs to another graph.</summary>
		public RenderGraphPass? GetPass(RenderGraphPassHandle handle)
		{
			if (!handle.IsValid || handle.Index >= passes.Count)
			{
				return null;
			}
			return passes[handle.Index];
		}

		/// <summary>
		/// Works out what the frame actually needs and in what order, and returns
		/// false when the graph cannot run (no pass renders into the back buffer,
		/// or the dependencies contain a cycle). A false result is final for the
		/// frame: <see cref="Execute"/> refuses to run an uncompiled graph, and the
		/// window keeps showing the cleared background.
		///
		/// Compiling is idempotent - calling it twice on unchanged passes produces
		/// the same order - and it is where the interesting bookkeeping happens:
		/// culling, the stable topological sort, the resource lifetimes and the
		/// peak live-resource count. Cost is a few passes over a list of tens of
		/// entries, on scratch the graph already owns.
		/// </summary>
		public bool Compile()
		{
			isCompiled = false;
			executionOrder.Clear();
			culledPassCount = 0;
			executedPassCount = 0;
			peakLiveResourceCount = 0;

			for (int resourceIndex = 0; resourceIndex < resources.Count; ++resourceIndex)
			{
				resources[resourceIndex].ResetLifetime();
			}

			if (passes.Count == 0)
			{
				Debug.LogError("{0}: the graph has no passes, so it cannot produce a frame.", logPrefix);
				return false;
			}

			int backBufferPassCount = 0;
			for (int passIndex = 0; passIndex < passes.Count; ++passIndex)
			{
				RenderGraphPass pass = passes[passIndex];
				pass.ResetCompiledState();
				if (IsBackBufferPass(pass))
				{
					++backBufferPassCount;
				}
			}

			if (backBufferPassCount == 0)
			{
				Debug.LogError(
					"{0}: no pass renders into the back buffer, and the back buffer is the only render target the backend has.",
					logPrefix);
				return false;
			}

			EnsureScratchCapacity();
			MarkContributingPasses();
			BuildOrderingEdges();

			if (!SortPasses())
			{
				return false;
			}

			ApplyCulling();
			ComputeResourceLifetimes();
			ComputePeakLiveResourceCount();
			isCompiled = true;

			// Nothing is logged on the success path: a pipeline compiles its graph
			// once per frame, so a per-compile entry would bury the log. A caller
			// that wants the details asks for GetDebugSummary() when it matters.

			return true;
		}

		/// <summary>
		/// Runs the compiled passes in order, handing each one the context it
		/// records through. A pass that throws is reported and skipped, and the
		/// rest of the frame still records - a broken effect must not cost the
		/// whole frame, which is the same rule the pipeline manager applies to a
		/// broken pipeline.
		///
		/// Nothing is submitted here: closing the frame stays the pipeline
		/// manager's job, so a pipeline can record more work after the graph ran.
		/// </summary>
		public void Execute(ScriptableRenderContext context)
		{
			if (context == null)
			{
				Debug.LogError("{0}: Execute needs a scriptable render context.", logPrefix);
				return;
			}

			if (!isCompiled)
			{
				Debug.LogError("{0}: Execute was called on a graph that is not compiled; call Compile() first.", logPrefix);
				return;
			}

			// The context is a view of one frame's recording, so it is reused as
			// long as the frame it points at is the same one; a new frame gets a
			// new view instead of a stale command buffer.
			if (executionContext == null || !ReferenceEquals(executionContext.ScriptableRenderContext, context))
			{
				executionContext = new RenderGraphContext(this, context);
			}

			executedPassCount = 0;
			for (int orderIndex = 0; orderIndex < executionOrder.Count; ++orderIndex)
			{
				RenderGraphPass pass = passes[executionOrder[orderIndex]];
				executionContext.BeginPass(pass, context.BackbufferWidth, context.BackbufferHeight);
				++executedPassCount;

				Action<RenderGraphContext>? execute = pass.Execute;
				if (execute == null)
				{
					executionContext.EndPass();
					continue;
				}

				try
				{
					execute(executionContext);
				}
				catch (Exception exception)
				{
					Debug.LogError(
						"{0}: pass '{1}' threw and was skipped: {2}",
						logPrefix, pass.Name, exception.Message);
				}
				finally
				{
					executionContext.EndPass();
				}
			}
		}

		/// <summary>
		/// Empties the graph so the same instance can be rebuilt for the next
		/// frame: passes, declared textures, compiled order and every counter go
		/// back to what a fresh graph has, and the two swapchain resources are
		/// created again. Pipelines call it at the top of their render method,
		/// which is why rebuilding a graph costs no allocation after the first
		/// frame.
		/// </summary>
		public void Reset()
		{
			passes.Clear();
			resources.Clear();
			executionOrder.Clear();
			pendingPassIndices.Clear();
			edgeSources.Clear();
			edgeTargets.Clear();
			summaryBuilder.Clear();
			executionContext = null;
			isCompiled = false;
			culledPassCount = 0;
			executedPassCount = 0;
			peakLiveResourceCount = 0;
			CreateBackBufferResources();
		}

		/// <summary>
		/// A multi-line report of the whole graph: the counters, every resource
		/// with its kind, size, bindless slot and lifetime, and every pass with the
		/// order it runs in (or the fact that it was culled), what it reads and
		/// what it writes. It is meant for an assert, a log line after a frame that
		/// looked wrong, or a pipeline debugging its own culling.
		///
		/// The string is built in a buffer the graph owns, so it is a snapshot: a
		/// caller that keeps it keeps the text, not a view.
		/// </summary>
		public string GetDebugSummary()
		{
			summaryBuilder.Clear();
			summaryBuilder.Append(LogTag).Append(" '").Append(name).Append("' [");
			summaryBuilder.Append(isCompiled ? "compiled" : "not compiled");
			summaryBuilder.Append("]: ").Append(passes.Count).Append(" passes, ");
			summaryBuilder.Append(culledPassCount).Append(" culled, ");
			summaryBuilder.Append(executionOrder.Count).Append(" in the execution order, peak ");
			summaryBuilder.Append(peakLiveResourceCount).Append(" live resources, ");
			summaryBuilder.Append(executedPassCount).Append(" executed by the last Execute.");
			summaryBuilder.Append('\n');

			AppendResourceLines();
			AppendPassLines();
			return summaryBuilder.ToString();
		}

		// -------- Pass authoring, called by RenderGraphPassBuilder --------

		/// <summary>Renames a pass; called by the builder.</summary>
		internal void SetPassName(int passIndex, string passName)
		{
			RenderGraphPass? pass = GetAuthoringPass(passIndex);
			if (pass == null)
			{
				return;
			}

			if (string.IsNullOrEmpty(passName))
			{
				Debug.LogWarning("{0}: pass '{1}' was given an empty name; it keeps the old one.", logPrefix, pass.Name);
				return;
			}
			pass.Name = passName;
		}

		/// <summary>Labels a pass; called by the builder.</summary>
		internal void SetPassKind(int passIndex, RenderGraphPassKind kind)
		{
			RenderGraphPass? pass = GetAuthoringPass(passIndex);
			if (pass != null)
			{
				pass.Kind = kind;
			}
		}

		/// <summary>Marks a pass as rendering into the swapchain; called by the builder.</summary>
		internal void SetPassWritesBackBuffer(int passIndex)
		{
			RenderGraphPass? pass = GetAuthoringPass(passIndex);
			if (pass != null)
			{
				pass.WritesBackBuffer = true;
			}
		}

		/// <summary>Declares a read of a resource; called by the builder.</summary>
		internal void AddPassRead(int passIndex, RenderGraphTextureHandle handle)
		{
			RenderGraphPass? pass = GetAuthoringPass(passIndex);
			if (pass == null)
			{
				return;
			}

			if (!pass.TryAddRead(handle, resources.Count))
			{
				ReportUnusableHandle(pass, handle, "read");
			}
		}

		/// <summary>Declares a write of a resource; called by the builder.</summary>
		internal void AddPassWrite(int passIndex, RenderGraphTextureHandle handle)
		{
			RenderGraphPass? pass = GetAuthoringPass(passIndex);
			if (pass == null)
			{
				return;
			}

			if (!pass.TryAddWrite(handle, resources.Count))
			{
				ReportUnusableHandle(pass, handle, "write");
			}
		}

		/// <summary>Stores the callback a pass records with; called by the builder.</summary>
		internal void SetPassExecute(int passIndex, Action<RenderGraphContext>? execute)
		{
			RenderGraphPass? pass = GetAuthoringPass(passIndex);
			if (pass == null)
			{
				return;
			}

			if (execute == null)
			{
				Debug.LogWarning("{0}: pass '{1}' was given a null callback and records nothing.", logPrefix, pass.Name);
				return;
			}
			pass.Execute = execute;
		}

		/// <summary>Switches a pass on or off; called by the builder.</summary>
		internal void SetPassEnabled(int passIndex, bool isEnabled)
		{
			RenderGraphPass? pass = GetAuthoringPass(passIndex);
			if (pass != null)
			{
				pass.IsEnabled = isEnabled;
			}
		}

		/// <summary>Finishes a pass and reports one that can never contribute; called by the builder.</summary>
		internal void CompletePass(int passIndex)
		{
			RenderGraphPass? pass = GetAuthoringPass(passIndex);
			if (pass == null)
			{
				return;
			}

			if (pass.Execute == null)
			{
				Debug.LogWarning("{0}: pass '{1}' was submitted without a callback, so it records nothing.", logPrefix, pass.Name);
			}
			else if (!pass.WritesBackBuffer && pass.WrittenTextures.Count == 0)
			{
				Debug.LogWarning(
					"{0}: pass '{1}' declares no writes and no back-buffer render target, so Compile() will cull it.",
					logPrefix, pass.Name);
			}
		}

		/// <summary>Bindless slot of a resource, for the context (-1 when the index names nothing).</summary>
		internal int GetResourceBindlessSlot(int resourceIndex)
		{
			if (resourceIndex < 0 || resourceIndex >= resources.Count)
			{
				return -1;
			}
			return resources[resourceIndex].BindlessSlot;
		}

		// -------- Compile internals --------

		/// <summary>Creates the two swapchain resources; they exist from the first moment a graph does.</summary>
		private void CreateBackBufferResources()
		{
			// -1 stands for "follows the swapchain": the real size is only known
			// while a frame is being rendered, and the graph never allocates it.
			resources.Add(new RenderGraphResource("BackBuffer.Color", RenderGraphResourceKind.BackBufferColor, -1, -1, -1));
			resources.Add(new RenderGraphResource("BackBuffer.Depth", RenderGraphResourceKind.BackBufferDepth, -1, -1, -1));
		}


		/// <summary>
		/// Culling: start from every pass that renders into the swapchain, then
		/// walk the edges backwards - a live pass reading a texture makes that
		/// texture's producer live too, and so on until nothing new turns up. What
		/// the walk never reaches cannot reach the screen.
		/// </summary>
		private void MarkContributingPasses()
		{
			for (int passIndex = 0; passIndex < passes.Count; ++passIndex)
			{
				alivePasses[passIndex] = false;
			}

			pendingPassIndices.Clear();
			for (int passIndex = 0; passIndex < passes.Count; ++passIndex)
			{
				RenderGraphPass pass = passes[passIndex];
				if (pass.IsEnabled && IsBackBufferPass(pass))
				{
					MarkPassAlive(passIndex);
				}
			}

			while (pendingPassIndices.Count > 0)
			{
				int passIndex = pendingPassIndices[pendingPassIndices.Count - 1];
				pendingPassIndices.RemoveAt(pendingPassIndices.Count - 1);

				IReadOnlyList<RenderGraphTextureHandle> reads = passes[passIndex].ReadTextures;
				for (int readIndex = 0; readIndex < reads.Count; ++readIndex)
				{
					int resourceIndex = reads[readIndex].Index;
					if (resourceIndex < 0 || resourceIndex >= resources.Count)
					{
						continue;
					}

					int writerIndex = FindProducer(resourceIndex, passIndex);
					if (writerIndex >= 0 && !alivePasses[writerIndex])
					{
						MarkPassAlive(writerIndex);
					}
				}
			}
		}

		/// <summary>Marks a pass as contributing and remembers it still has to be walked.</summary>
		private void MarkPassAlive(int passIndex)
		{
			alivePasses[passIndex] = true;
			pendingPassIndices.Add(passIndex);
		}

		/// <summary>
		/// The pass that fills a resource for one reader: the last enabled pass that
		/// writes it and is not the reader itself. Two details matter here. Skipping
		/// the reader is what lets a pass read and write the same texture - an
		/// in-place effect - and wait for the pass that filled it instead of
		/// depending on itself. And taking the LAST writer is what makes an earlier
		/// write that a later one overwrites keep nothing alive, because no reader
		/// can ever see it.
		///
		/// The scan runs backwards over the passes rather than over a table built up
		/// front: a graph holds tens of passes, so the scan is free, and it needs no
		/// table that a discovered pass would have to invalidate.
		/// </summary>
		private int FindProducer(int resourceIndex, int readerPassIndex)
		{
			for (int passIndex = passes.Count - 1; passIndex >= 0; --passIndex)
			{
				if (passIndex == readerPassIndex || !passes[passIndex].IsEnabled)
				{
					continue;
				}

				if (PassWritesResource(passes[passIndex], resourceIndex))
				{
					return passIndex;
				}
			}

			return -1;
		}

		/// <summary>
		/// Dependency edges between surviving passes: a writer runs before every
		/// reader of the same resource, and the writers of one resource keep their
		/// declared order. Readers of a resource every writer feeds is the
		/// conservative rule - the backend has no resource versions, so the graph
		/// never guesses which write a read meant.
		/// </summary>
		private void BuildOrderingEdges()
		{
			edgeSources.Clear();
			edgeTargets.Clear();

			for (int resourceIndex = 0; resourceIndex < resources.Count; ++resourceIndex)
			{
				int previousWriter = -1;
				for (int passIndex = 0; passIndex < passes.Count; ++passIndex)
				{
					if (!alivePasses[passIndex] || !PassWritesResource(passes[passIndex], resourceIndex))
					{
						continue;
					}

					if (previousWriter >= 0)
					{
						AddEdge(previousWriter, passIndex);
					}
					previousWriter = passIndex;
				}
			}

			for (int passIndex = 0; passIndex < passes.Count; ++passIndex)
			{
				if (!alivePasses[passIndex])
				{
					continue;
				}

				IReadOnlyList<RenderGraphTextureHandle> reads = passes[passIndex].ReadTextures;
				for (int readIndex = 0; readIndex < reads.Count; ++readIndex)
				{
					int resourceIndex = reads[readIndex].Index;
					if (resourceIndex < 0 || resourceIndex >= resources.Count)
					{
						continue;
					}

					for (int writerIndex = 0; writerIndex < passes.Count; ++writerIndex)
					{
						// A pass that reads and writes the same texture is one
						// node, not an edge to itself.
						if (writerIndex == passIndex || !alivePasses[writerIndex])
						{
							continue;
						}

						if (PassWritesResource(passes[writerIndex], resourceIndex))
						{
							AddEdge(writerIndex, passIndex);
						}
					}
				}
			}
		}

		/// <summary>Adds one dependency edge, ignoring a duplicate so in-degrees stay correct.</summary>
		private void AddEdge(int sourcePassIndex, int targetPassIndex)
		{
			for (int edgeIndex = 0; edgeIndex < edgeSources.Count; ++edgeIndex)
			{
				if (edgeSources[edgeIndex] == sourcePassIndex && edgeTargets[edgeIndex] == targetPassIndex)
				{
					return;
				}
			}

			edgeSources.Add(sourcePassIndex);
			edgeTargets.Add(targetPassIndex);
		}

		/// <summary>
		/// Stable topological sort of the surviving passes: at every step the
		/// lowest-numbered pass whose producers all ran is taken, so passes that do
		/// not depend on each other come out in the order they were added - the
		/// order the pipeline author wrote. When no pass is ready but passes are
		/// left, those passes sit in or behind a cycle, which is reported.
		/// </summary>
		private bool SortPasses()
		{
			for (int passIndex = 0; passIndex < passes.Count; ++passIndex)
			{
				inDegrees[passIndex] = 0;
				emittedPasses[passIndex] = false;
			}

			for (int edgeIndex = 0; edgeIndex < edgeSources.Count; ++edgeIndex)
			{
				++inDegrees[edgeTargets[edgeIndex]];
			}

			int alivePassCount = 0;
			for (int passIndex = 0; passIndex < passes.Count; ++passIndex)
			{
				if (alivePasses[passIndex])
				{
					++alivePassCount;
				}
			}

			for (int step = 0; step < alivePassCount; ++step)
			{
				int nextPassIndex = -1;
				for (int passIndex = 0; passIndex < passes.Count; ++passIndex)
				{
					if (alivePasses[passIndex] && !emittedPasses[passIndex] && inDegrees[passIndex] == 0)
					{
						nextPassIndex = passIndex;
						break;
					}
				}

				if (nextPassIndex < 0)
				{
					ReportDependencyCycle();
					return false;
				}

				emittedPasses[nextPassIndex] = true;
				executionOrder.Add(nextPassIndex);

				for (int edgeIndex = 0; edgeIndex < edgeSources.Count; ++edgeIndex)
				{
					if (edgeSources[edgeIndex] == nextPassIndex)
					{
						--inDegrees[edgeTargets[edgeIndex]];
					}
				}
			}

			return true;
		}

		/// <summary>Reports the passes a cycle kept from ever becoming ready; the error path only.</summary>
		private void ReportDependencyCycle()
		{
			StringBuilder message = new StringBuilder();
			message.Append(logPrefix).Append(": the graph contains a dependency cycle; these passes never became ready: ");

			bool isFirst = true;
			for (int passIndex = 0; passIndex < passes.Count; ++passIndex)
			{
				if (!alivePasses[passIndex] || emittedPasses[passIndex])
				{
					continue;
				}

				if (!isFirst)
				{
					message.Append(", ");
				}
				message.Append('\'').Append(passes[passIndex].Name).Append('\'');
				isFirst = false;
			}

			message.Append(". The frame was left empty; break the cycle by removing one of the reads that closes it.");
			Debug.LogError(message.ToString());
		}

		/// <summary>Writes the cull result onto the passes and counts it.</summary>
		private void ApplyCulling()
		{
			culledPassCount = 0;
			for (int passIndex = 0; passIndex < passes.Count; ++passIndex)
			{
				RenderGraphPass pass = passes[passIndex];
				pass.IsCulled = !alivePasses[passIndex];
				if (pass.IsCulled)
				{
					++culledPassCount;
				}
			}

			for (int passIndex = 0; passIndex < passes.Count; ++passIndex)
			{
				orderOfPass[passIndex] = -1;
			}
			for (int orderIndex = 0; orderIndex < executionOrder.Count; ++orderIndex)
			{
				orderOfPass[executionOrder[orderIndex]] = orderIndex;
			}
		}

		/// <summary>
		/// First and last use of every resource, measured in positions of the
		/// execution order rather than in passes: that is the timeline the frame
		/// actually runs on, so it is the one a lifetime means anything on.
		/// </summary>
		private void ComputeResourceLifetimes()
		{
			for (int orderIndex = 0; orderIndex < executionOrder.Count; ++orderIndex)
			{
				RenderGraphPass pass = passes[executionOrder[orderIndex]];

				if (pass.WritesBackBuffer)
				{
					TouchResourceLifetime(BackBufferColorResourceIndex, orderIndex);
				}

				TouchResourceLifetime(pass.ReadTextures, orderIndex);
				TouchResourceLifetime(pass.WrittenTextures, orderIndex);
			}
		}

		/// <summary>Extends one resource's lifetime to cover the given position.</summary>
		private void TouchResourceLifetime(int resourceIndex, int orderIndex)
		{
			if (resourceIndex < 0 || resourceIndex >= resources.Count)
			{
				return;
			}

			RenderGraphResource resource = resources[resourceIndex];
			if (resource.FirstUseOrderIndex < 0 || orderIndex < resource.FirstUseOrderIndex)
			{
				resource.FirstUseOrderIndex = orderIndex;
			}
			if (orderIndex > resource.LastUseOrderIndex)
			{
				resource.LastUseOrderIndex = orderIndex;
			}
		}

		/// <summary>Extends every listed resource's lifetime to cover the given position.</summary>
		private void TouchResourceLifetime(IReadOnlyList<RenderGraphTextureHandle> handles, int orderIndex)
		{
			for (int handleIndex = 0; handleIndex < handles.Count; ++handleIndex)
			{
				TouchResourceLifetime(handles[handleIndex].Index, orderIndex);
			}
		}

		/// <summary>Peak number of resources whose lifetime covers one position of the execution order.</summary>
		private void ComputePeakLiveResourceCount()
		{
			peakLiveResourceCount = 0;
			for (int orderIndex = 0; orderIndex < executionOrder.Count; ++orderIndex)
			{
				int liveResourceCount = 0;
				for (int resourceIndex = 0; resourceIndex < resources.Count; ++resourceIndex)
				{
					if (resources[resourceIndex].IsLiveAtOrderIndex(orderIndex))
					{
						++liveResourceCount;
					}
				}

				if (liveResourceCount > peakLiveResourceCount)
				{
					peakLiveResourceCount = liveResourceCount;
				}
			}
		}

		/// <summary>
		/// True when the pass claims the swapchain: either through
		/// <see cref="RenderGraphPassBuilder.WriteBackBuffer"/> or by declaring a
		/// write of one of the two back-buffer resources by hand.
		/// </summary>
		private static bool IsBackBufferPass(RenderGraphPass pass)
		{
			return pass.WritesBackBuffer
				|| pass.WritesTexture(BackBufferColorResourceIndex)
				|| pass.WritesTexture(BackBufferDepthResourceIndex);
		}

		/// <summary>True when the pass produces the given resource; writing the back buffer counts as writing its color slice.</summary>
		private static bool PassWritesResource(RenderGraphPass pass, int resourceIndex)
		{
			if (pass.WritesBackBuffer && resourceIndex == BackBufferColorResourceIndex)
			{
				return true;
			}
			return pass.WritesTexture(resourceIndex);
		}

		/// <summary>
		/// Looks a pass up for an authoring call. Returns null - and says why in the
		/// log - for a builder that outlived its graph or its frame, so a stale
		/// builder can never touch a pass it does not own.
		/// </summary>
		private RenderGraphPass? GetAuthoringPass(int passIndex)
		{
			if (isCompiled)
			{
				Debug.LogWarning(
					"{0}: the graph is compiled, so pass authoring is ignored; call Reset() before building the next frame.",
					logPrefix);
				return null;
			}

			if (passIndex < 0 || passIndex >= passes.Count)
			{
				Debug.LogWarning("{0}: a pass builder pointing at index {1} is stale; the call was ignored.", logPrefix, passIndex);
				return null;
			}

			return passes[passIndex];
		}

		/// <summary>Reports a handle a pass could not declare; the error path only.</summary>
		private void ReportUnusableHandle(RenderGraphPass pass, RenderGraphTextureHandle handle, string accessKind)
		{
			if (!handle.IsValid || handle.Index >= resources.Count)
			{
				Debug.LogWarning(
					"{0}: pass '{1}' declared a {2} of handle {3}, which names no resource of this graph.",
					logPrefix, pass.Name, accessKind, handle.Index);
				return;
			}

			Debug.LogWarning(
				"{0}: pass '{1}' already declared a {2} of resource '{3}', so the repeated declaration was ignored.",
				logPrefix, pass.Name, accessKind, resources[handle.Index].Name);
		}

		/// <summary>Finds a resource by name, for the duplicate-name warning (-1 when there is none).</summary>
		private int FindResourceIndex(string resourceName)
		{
			for (int resourceIndex = 0; resourceIndex < resources.Count; ++resourceIndex)
			{
				if (string.Equals(resources[resourceIndex].Name, resourceName, StringComparison.Ordinal))
				{
					return resourceIndex;
				}
			}
			return -1;
		}

		/// <summary>Grows the compile scratch when the graph grew; the entries used are always re-initialised by the compile.</summary>
		private void EnsureScratchCapacity()
		{
			if (alivePasses.Length < passes.Count)
			{
				alivePasses = new bool[passes.Count];
				emittedPasses = new bool[passes.Count];
				inDegrees = new int[passes.Count];
				orderOfPass = new int[passes.Count];
			}
		}

		/// <summary>Appends one line per resource to the summary being built.</summary>
		private void AppendResourceLines()
		{
			summaryBuilder.Append("  resources:\n");
			for (int resourceIndex = 0; resourceIndex < resources.Count; ++resourceIndex)
			{
				RenderGraphResource resource = resources[resourceIndex];
				summaryBuilder.Append("    [").Append(resourceIndex).Append("] ").Append(resource.ToString());
				if (resource.IsUsed)
				{
					summaryBuilder.Append(", live over order ").Append(resource.FirstUseOrderIndex);
					summaryBuilder.Append("..").Append(resource.LastUseOrderIndex);
				}
				else
				{
					summaryBuilder.Append(", unused");
				}
				summaryBuilder.Append('\n');
			}
		}

		/// <summary>Appends one line per pass to the summary being built.</summary>
		private void AppendPassLines()
		{
			summaryBuilder.Append("  passes:\n");
			for (int passIndex = 0; passIndex < passes.Count; ++passIndex)
			{
				RenderGraphPass pass = passes[passIndex];
				summaryBuilder.Append("    [").Append(passIndex).Append("] ");

				if (pass.IsCulled)
				{
					summaryBuilder.Append(pass.IsEnabled ? "culled" : "culled (disabled)");
				}
				else
				{
					summaryBuilder.Append("order ").Append(orderOfPass.Length > passIndex ? orderOfPass[passIndex] : -1);
				}

				summaryBuilder.Append(": '").Append(pass.Name).Append("' (").Append(pass.Kind).Append(')');
				if (pass.WritesBackBuffer)
				{
					summaryBuilder.Append(" writes back buffer,");
				}

				summaryBuilder.Append(" reads: ");
				AppendHandleList(pass.ReadTextures);
				summaryBuilder.Append(", writes: ");
				AppendHandleList(pass.WrittenTextures);
				summaryBuilder.Append('\n');
			}
		}

		/// <summary>Appends a comma-separated list of resource names, or "(none)".</summary>
		private void AppendHandleList(IReadOnlyList<RenderGraphTextureHandle> handles)
		{
			if (handles.Count == 0)
			{
				summaryBuilder.Append("(none)");
				return;
			}

			for (int handleIndex = 0; handleIndex < handles.Count; ++handleIndex)
			{
				if (handleIndex > 0)
				{
					summaryBuilder.Append(", ");
				}

				int resourceIndex = handles[handleIndex].Index;
				if (resourceIndex >= 0 && resourceIndex < resources.Count)
				{
					summaryBuilder.Append('\'').Append(resources[resourceIndex].Name).Append('\'');
				}
				else
				{
					summaryBuilder.Append("(stale handle ").Append(resourceIndex).Append(')');
				}
			}
		}
	}
}

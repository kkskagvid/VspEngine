using System;
using System.Collections.Generic;

namespace VspEngine.Rendering
{
	/// <summary>
	/// Handle to one pass of a <see cref="RenderGraph"/>.
	///
	/// Like <see cref="RenderGraphTextureHandle"/> this is a plain index into the
	/// graph that created it: no native object, no lifetime to manage, nothing to
	/// dispose, and meaningful only for that one graph.
	/// </summary>
	public readonly struct RenderGraphPassHandle : IEquatable<RenderGraphPassHandle>
	{
		/// <summary>Index of the pass inside its graph (-1 = names no pass).</summary>
		public readonly int Index;

		/// <summary>Wraps a pass index; the graph itself is the intended caller.</summary>
		public RenderGraphPassHandle(int index)
		{
			Index = index;
		}

		/// <summary>A handle that names no pass.</summary>
		public static RenderGraphPassHandle Invalid => new RenderGraphPassHandle(-1);

		/// <summary>True when this handle names a pass of its graph.</summary>
		public bool IsValid => Index >= 0;

		public bool Equals(RenderGraphPassHandle other) => Index == other.Index;

		public override bool Equals(object? obj) => obj is RenderGraphPassHandle other && Equals(other);

		public override int GetHashCode() => Index;

		public override string ToString() => IsValid ? "Pass#" + Index : "Pass#invalid";
	}

	/// <summary>
	/// What a pass does, for the reader of a pipeline and for tooling.
	///
	/// The kind is a label and never a scheduling rule: the graph orders passes
	/// by the resources they declare, so a Compute pass and a Raster pass that
	/// touch the same texture are ordered identically whether or not they are
	/// labelled correctly. Reporting the wrong kind costs nothing but a
	/// misleading debug summary.
	/// </summary>
	public enum RenderGraphPassKind
	{
		/// <summary>Draws geometry into the frame's render target.</summary>
		Raster = 0,

		/// <summary>Does work that draws no geometry (the backend has no compute dispatch yet).</summary>
		Compute = 1,

		/// <summary>Hands the finished back buffer to the presentation engine.</summary>
		Present = 2,

		/// <summary>Clears a target instead of drawing into it.</summary>
		Clear = 3,

		/// <summary>Anything the other kinds do not describe.</summary>
		Custom = 4,
	}

	/// <summary>
	/// One pass of a <see cref="RenderGraph"/>: a name, a kind, the textures it
	/// reads, the textures it writes, whether it renders into the back buffer and
	/// the callback that records it. A pass owns no graphics resource - the
	/// pipeline owns those - so a pass is only ever a description plus a closure.
	///
	/// Passes are created through <see cref="RenderGraph.AddPass"/>, which hands
	/// back a <see cref="RenderGraphPassBuilder"/>; the graph fills this object
	/// in as the builder is configured and again when it compiles the frame.
	/// </summary>
	public sealed class RenderGraphPass
	{
		private readonly List<RenderGraphTextureHandle> readTextures = new List<RenderGraphTextureHandle>();
		private readonly List<RenderGraphTextureHandle> writtenTextures = new List<RenderGraphTextureHandle>();

		internal RenderGraphPass(int index, string name)
		{
			Index = index;
			Name = name;
		}

		/// <summary>Index of the pass inside its graph, which is also its index in <see cref="RenderGraph.Passes"/>.</summary>
		public int Index { get; }

		/// <summary>Name of the pass; it becomes <see cref="RenderGraphContext.PassName"/> while the pass records.</summary>
		public string Name { get; internal set; }

		/// <summary>What kind of work the pass does (a label, see <see cref="RenderGraphPassKind"/>).</summary>
		public RenderGraphPassKind Kind { get; internal set; } = RenderGraphPassKind.Raster;

		/// <summary>
		/// True when the pass renders into the swapchain back buffer - the only
		/// render target this backend has, and the reason a graph exists at all:
		/// a pass that contributes to no back-buffer pass is culled.
		/// </summary>
		public bool WritesBackBuffer { get; internal set; }

		/// <summary>Textures the pass declared as reads; the graph orders it after whatever writes them.</summary>
		public IReadOnlyList<RenderGraphTextureHandle> ReadTextures => readTextures;

		/// <summary>Textures the pass declared as writes; these are what keep it alive.</summary>
		public IReadOnlyList<RenderGraphTextureHandle> WrittenTextures => writtenTextures;

		/// <summary>
		/// True when the pass does not run this frame: either its author disabled
		/// it with <see cref="RenderGraphPassBuilder.SetEnabled"/>, or
		/// <see cref="RenderGraph.Compile"/> found that nothing it produces
		/// reaches the back buffer. Culled passes are still listed, so a debug
		/// summary shows what the graph dropped.
		/// </summary>
		public bool IsCulled { get; internal set; }

		/// <summary>False when the authoring code switched the pass off; a disabled pass is always culled.</summary>
		public bool IsEnabled { get; internal set; } = true;

		/// <summary>The callback the graph invokes while the pass records, or null when the pass records nothing.</summary>
		internal Action<RenderGraphContext>? Execute { get; set; }

		/// <summary>Handle that names this pass.</summary>
		internal RenderGraphPassHandle Handle => new RenderGraphPassHandle(Index);

		/// <summary>Drops the state a previous compile wrote, so the next compile starts clean.</summary>
		internal void ResetCompiledState()
		{
			IsCulled = false;
		}

		/// <summary>True when the pass declared the given resource as a read.</summary>
		internal bool DeclaresRead(RenderGraphTextureHandle handle)
		{
			for (int readIndex = 0; readIndex < readTextures.Count; ++readIndex)
			{
				if (readTextures[readIndex].Index == handle.Index)
				{
					return true;
				}
			}
			return false;
		}

		/// <summary>True when the pass declared the given resource as a write.</summary>
		internal bool WritesTexture(int resourceIndex)
		{
			for (int writeIndex = 0; writeIndex < writtenTextures.Count; ++writeIndex)
			{
				if (writtenTextures[writeIndex].Index == resourceIndex)
				{
					return true;
				}
			}
			return false;
		}

		/// <summary>Adds a read; a repeated or out-of-range handle is ignored instead of throwing.</summary>
		internal bool TryAddRead(RenderGraphTextureHandle handle, int resourceCount)
		{
			if (!handle.IsValid || handle.Index >= resourceCount || DeclaresRead(handle))
			{
				return false;
			}
			readTextures.Add(handle);
			return true;
		}

		/// <summary>Adds a write; a repeated or out-of-range handle is ignored instead of throwing.</summary>
		internal bool TryAddWrite(RenderGraphTextureHandle handle, int resourceCount)
		{
			if (!handle.IsValid || handle.Index >= resourceCount || WritesTexture(handle.Index))
			{
				return false;
			}
			writtenTextures.Add(handle);
			return true;
		}

		/// <summary>One-line description, as <see cref="RenderGraph.GetDebugSummary"/> prints it.</summary>
		public override string ToString() => "'" + Name + "' (" + Kind + ")";
	}

	/// <summary>
	/// Configures one pass while the graph is being built. Every method returns
	/// the builder again, so a pass is authored as one fluent statement, and every
	/// method tolerates a stale or default builder by doing nothing and saying so
	/// in the log, because the render path never throws.
	///
	/// The pass exists in the graph from the moment <see cref="RenderGraph.AddPass"/>
	/// returns, so a builder that is never finished with <see cref="Done"/> still
	/// produces a pass; <see cref="Done"/> only lets the graph warn about a pass
	/// that was configured into something useless (no callback, or no writes).
	/// </summary>
	public readonly struct RenderGraphPassBuilder
	{
		private readonly RenderGraph? graph;
		private readonly int passIndex;

		internal RenderGraphPassBuilder(RenderGraph graph, int passIndex)
		{
			this.graph = graph;
			this.passIndex = passIndex;
		}

		/// <summary>The handle of the pass being built (invalid for a default builder).</summary>
		public RenderGraphPassHandle Handle => graph != null ? new RenderGraphPassHandle(passIndex) : RenderGraphPassHandle.Invalid;

		/// <summary>Renames the pass, which is what the debug summary and the context report.</summary>
		public RenderGraphPassBuilder SetName(string name)
		{
			graph?.SetPassName(passIndex, name);
			return this;
		}

		/// <summary>Labels what the pass does; see <see cref="RenderGraphPassKind"/> for why the label does not schedule anything.</summary>
		public RenderGraphPassBuilder SetKind(RenderGraphPassKind kind)
		{
			graph?.SetPassKind(passIndex, kind);
			return this;
		}

		/// <summary>
		/// Declares that the pass renders into the swapchain back buffer. A pass
		/// that makes this claim is a root of the graph: it is never culled, and
		/// it is what keeps the passes producing its inputs alive.
		/// </summary>
		public RenderGraphPassBuilder WriteBackBuffer()
		{
			graph?.SetPassWritesBackBuffer(passIndex);
			return this;
		}

		/// <summary>
		/// Declares a texture the pass samples. Reading a resource makes the pass
		/// run after whatever writes it, and pulls that producer into the frame.
		/// </summary>
		public RenderGraphPassBuilder ReadTexture(RenderGraphTextureHandle handle)
		{
			graph?.AddPassRead(passIndex, handle);
			return this;
		}

		/// <summary>
		/// Declares a texture the pass fills. A write nothing reads is dead work,
		/// so a pass whose writes all stay unread is culled unless it also renders
		/// into the back buffer.
		/// </summary>
		public RenderGraphPassBuilder WriteTexture(RenderGraphTextureHandle handle)
		{
			graph?.AddPassWrite(passIndex, handle);
			return this;
		}

		/// <summary>
		/// Sets what the pass records. The callback runs at
		/// <see cref="RenderGraph.Execute"/> time, once, with a context that
		/// already names the pass and its size; a callback that throws is reported
		/// and skipped, and the rest of the frame still records.
		/// </summary>
		public RenderGraphPassBuilder SetExecute(Action<RenderGraphContext> execute)
		{
			graph?.SetPassExecute(passIndex, execute);
			return this;
		}

		/// <summary>
		/// Switches the pass off for this frame. A disabled pass produces nothing,
		/// so it is culled like any other pass that cannot reach the back buffer;
		/// it stays in the graph, which is what makes a feature toggle cheap.
		/// </summary>
		public RenderGraphPassBuilder SetEnabled(bool isEnabled)
		{
			graph?.SetPassEnabled(passIndex, isEnabled);
			return this;
		}

		/// <summary>
		/// Finishes the pass, which lets the graph report a pass that was
		/// configured into something that cannot contribute (no callback, or no
		/// writes at all). Calling it is optional; skipping it changes nothing
		/// except that no such warning is written.
		/// </summary>
		public void Done()
		{
			graph?.CompletePass(passIndex);
		}
	}
}

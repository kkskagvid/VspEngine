using System;

namespace VspEngine.Rendering
{
	/// <summary>
	/// Handle to one texture resource of a <see cref="RenderGraph"/>.
	///
	/// A handle is nothing but an index into the graph that created it: it owns
	/// no native object, costs nothing to copy and is meaningless for another
	/// graph. <see cref="IsValid"/> is false for the default value and for
	/// <see cref="Invalid"/>, which is what the authoring API hands back when it
	/// refused to create the resource.
	/// </summary>
	public readonly struct RenderGraphTextureHandle : IEquatable<RenderGraphTextureHandle>
	{
		/// <summary>Index of the resource inside its graph (-1 = names no resource).</summary>
		public readonly int Index;

		/// <summary>Wraps a resource index; the graph itself is the intended caller.</summary>
		public RenderGraphTextureHandle(int index)
		{
			Index = index;
		}

		/// <summary>A handle that names no resource.</summary>
		public static RenderGraphTextureHandle Invalid => new RenderGraphTextureHandle(-1);

		/// <summary>True when this handle names a resource of its graph.</summary>
		public bool IsValid => Index >= 0;

		public bool Equals(RenderGraphTextureHandle other) => Index == other.Index;

		public override bool Equals(object? obj) => obj is RenderGraphTextureHandle other && Equals(other);

		public override int GetHashCode() => Index;

		public override string ToString() => IsValid ? "Texture#" + Index : "Texture#invalid";
	}

	/// <summary>
	/// What a graph resource stands for.
	///
	/// The wrapped graphics API can render into the swapchain back buffer only
	/// and can create sampled bindless textures only, so these three kinds are
	/// the whole set the graph can describe: the color slice of the swapchain,
	/// its depth slice and one sampled image the game created.
	/// </summary>
	public enum RenderGraphResourceKind
	{
		/// <summary>No resource; the value of a default-initialised kind.</summary>
		None = 0,

		/// <summary>The swapchain color image the frame is drawn into.</summary>
		BackBufferColor = 1,

		/// <summary>The depth slice of the frame's render pass (the backend has no separate depth image yet).</summary>
		BackBufferDepth = 2,

		/// <summary>A sampled bindless texture that exists outside the graph.</summary>
		Texture = 3,
	}

	/// <summary>
	/// One resource declared in a <see cref="RenderGraph"/>: what it is, how big
	/// it is, which bindless slot shaders read it from and - once
	/// <see cref="RenderGraph.Compile"/> has run - for how long it stays alive.
	///
	/// Nothing here allocates graphics memory, and that is a deliberate
	/// consequence of the backend: there is no offscreen render target, so a
	/// texture resource is bookkeeping for a texture the game already created
	/// (<see cref="Texture2D"/> and its bindless slot), and the two back-buffer
	/// resources describe the swapchain image the passes render into. The
	/// lifetimes exist because they are what a render graph is for: they tell a
	/// pipeline how many transient targets a frame needs at once and where a
	/// target could be reused for another one, which is the information a future
	/// backend with real offscreen targets will alias memory with.
	/// </summary>
	public sealed class RenderGraphResource
	{
		internal RenderGraphResource(string name, RenderGraphResourceKind kind, int width, int height, int bindlessSlot)
		{
			Name = name;
			Kind = kind;
			Width = width;
			Height = height;
			BindlessSlot = bindlessSlot;
		}

		/// <summary>Name the graph was given for this resource; used by the debug summary.</summary>
		public string Name { get; }

		/// <summary>What the resource stands for.</summary>
		public RenderGraphResourceKind Kind { get; }

		/// <summary>Width in pixels; 0 = unknown, -1 = follows the swapchain.</summary>
		public int Width { get; }

		/// <summary>Height in pixels; 0 = unknown, -1 = follows the swapchain.</summary>
		public int Height { get; }

		/// <summary>Slot of the texture in the bindless sampled image array (-1 = none).</summary>
		public int BindlessSlot { get; }

		/// <summary>True for the two swapchain resources, which no pass may create or destroy.</summary>
		public bool IsBackBuffer =>
			Kind == RenderGraphResourceKind.BackBufferColor || Kind == RenderGraphResourceKind.BackBufferDepth;

		/// <summary>
		/// Position, in the compiled execution order, of the first pass that reads
		/// or writes this resource (-1 while no surviving pass uses it). Together
		/// with <see cref="LastUseOrderIndex"/> this is the resource's lifetime.
		/// </summary>
		public int FirstUseOrderIndex { get; internal set; } = -1;

		/// <summary>
		/// Position, in the compiled execution order, of the last pass that reads
		/// or writes this resource (-1 while no surviving pass uses it).
		/// </summary>
		public int LastUseOrderIndex { get; internal set; } = -1;

		/// <summary>True once a pass that survives culling references this resource.</summary>
		public bool IsUsed => FirstUseOrderIndex >= 0;

		/// <summary>
		/// True while the given position of the execution order lies inside this
		/// resource's lifetime - the property the peak live-resource count is
		/// computed from.
		/// </summary>
		public bool IsLiveAtOrderIndex(int orderIndex) =>
			IsUsed && orderIndex >= FirstUseOrderIndex && orderIndex <= LastUseOrderIndex;

		/// <summary>Drops the lifetime a previous compile computed.</summary>
		internal void ResetLifetime()
		{
			FirstUseOrderIndex = -1;
			LastUseOrderIndex = -1;
		}

		/// <summary>One-line description, as <see cref="RenderGraph.GetDebugSummary"/> prints it.</summary>
		public override string ToString() =>
			"'" + Name + "' (" + Kind + ", " + Width + "x" + Height + ", bindless slot " + BindlessSlot + ")";
	}
}

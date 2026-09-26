using System;
using System.Collections.Generic;
using System.Numerics;

namespace VspEngine.UI
{
	/// <summary>
	/// Base class of everything a UI is made of.
	///
	/// An element is a rectangle in PIXEL SPACE whose position is relative to its
	/// parent, plus a list of children that inherit that offset. A frame walks
	/// the tree twice, which is all the framework a flat interface needs:
	///
	///   1. <see cref="UpdateHierarchy"/> resolves absolute positions, runs hit
	///      testing against the pointer and lets a widget react (a button hovers,
	///      presses, clicks);
	///   2. <see cref="DrawHierarchy"/> appends the quads a widget is made of to
	///      the frame's <see cref="UiDrawList"/>.
	///
	/// A canvas is the root: it holds the theme, the font and the size of the
	/// render target, and it is what a render pipeline talks to.
	/// </summary>
	public abstract class UiElement
	{
		private readonly List<UiElement> children = new List<UiElement>();

		/// <summary>Name used by diagnostics and by the layout dumper.</summary>
		public string Name { get; set; } = string.Empty;

		/// <summary>Placement relative to the parent, in pixels.</summary>
		public UiRect Bounds { get; set; }

		/// <summary>Where this element ended up in canvas pixels (set by Update).</summary>
		public UiRect AbsoluteBounds { get; private set; }

		/// <summary>False hides the element and everything below it.</summary>
		public bool IsVisible { get; set; } = true;

		/// <summary>False greys the element out and keeps it from reacting.</summary>
		public bool IsEnabled { get; set; } = true;

		/// <summary>True when the pointer is inside the element's absolute bounds.</summary>
		public bool IsHovered { get; private set; }

		/// <summary>The canvas this element belongs to, or null while detached.</summary>
		public Canvas? OwningCanvas { get; private set; }

		/// <summary>The element this one is placed inside, or null at the root.</summary>
		public UiElement? Parent { get; private set; }

		/// <summary>Children, in draw order: the first child is drawn first.</summary>
		public IReadOnlyList<UiElement> Children => children;

		/// <summary>Where this element sits in canvas pixels.</summary>
		public Vector2 AbsolutePosition => AbsoluteBounds.Min;

		/// <summary>
		/// Adds a child; the child's bounds are then relative to this one, and the
		/// canvas of this subtree adopts it. Returns the child, so a caller can
		/// build and configure a widget in one expression.
		/// </summary>
		public TElement AddChild<TElement>(TElement child) where TElement : UiElement
		{
			child.Parent?.RemoveChild(child);
			child.Parent = this;
			child.OwningCanvas = OwningCanvas ?? (this as Canvas);
			children.Add(child);
			OnChildAdded(child);
			return child;
		}

		public bool RemoveChild(UiElement child)
		{
			if (child == null || !children.Remove(child))
			{
				return false;
			}

			child.Parent = null;
			child.OwningCanvas = null;
			return true;
		}

		public void ClearChildren() => children.Clear();

		/// <summary>
		/// Walks the subtree: resolves absolute positions, updates hover state and
		/// gives every widget its chance to react to the pointer.
		/// </summary>
		public void UpdateHierarchy(UiInputState input, UiRect parentAbsoluteBounds, UiTheme theme, bool isParentInteractive)
		{
			if (!IsVisible)
			{
				IsHovered = false;
				return;
			}

			AbsoluteBounds = Bounds.OffsetBy(parentAbsoluteBounds.Min);
			IsHovered = isParentInteractive && IsEnabled && AbsoluteBounds.Contains(input.MousePosition);

			OnUpdate(input, theme);

			for (int childIndex = 0; childIndex < children.Count; ++childIndex)
			{
				// A child is only interactive while its parent is: a disabled
				// panel disables what it holds.
				children[childIndex].UpdateHierarchy(input, AbsoluteBounds, theme, isParentInteractive && IsEnabled);
			}
		}

		/// <summary>Walks the subtree and appends every widget's quads to the list.</summary>
		public void DrawHierarchy(UiDrawList drawList, UiTheme theme, Matrix4x4 projection)
		{
			if (!IsVisible)
			{
				return;
			}

			OnDraw(drawList, theme, projection);

			for (int childIndex = 0; childIndex < children.Count; ++childIndex)
			{
				children[childIndex].DrawHierarchy(drawList, theme, projection);
			}
		}

		/// <summary>Called once per frame with the pointer state.</summary>
		protected virtual void OnUpdate(UiInputState input, UiTheme theme) { }

		/// <summary>Called once per frame while the element is visible.</summary>
		protected virtual void OnDraw(UiDrawList drawList, UiTheme theme, Matrix4x4 projection) { }

		/// <summary>
		/// Called when a child was attached. A widget that is attached to a
		/// subtree already inside a canvas hands that canvas down to everything
		/// the child brought with it - a button's label, a panel's buttons - which
		/// is what keeps <see cref="OwningCanvas"/> correct however deep a widget
		/// tree is built before it is added to the canvas.
		/// </summary>
		protected virtual void OnChildAdded(UiElement child)
		{
			if (OwningCanvas != null)
			{
				AdoptCanvas(OwningCanvas, child);
			}
		}

		/// <summary>Propagates this element's canvas down a freshly attached subtree.</summary>
		protected void AdoptCanvas(Canvas canvas, UiElement element)
		{
			element.OwningCanvas = canvas;
			for (int childIndex = 0; childIndex < element.children.Count; ++childIndex)
			{
				AdoptCanvas(canvas, element.children[childIndex]);
			}
		}
	}
}

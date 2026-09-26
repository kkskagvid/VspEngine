using System;
using System.Collections.Generic;
using System.Numerics;
using System.Runtime.InteropServices;

namespace VspEngine.UI
{
	/// <summary>
	/// A rectangle in UI PIXEL SPACE: the origin is the top-left corner of the
	/// canvas, +X points right and +Y points down, and one unit is one pixel of
	/// the render target.
	///
	/// Pixel space is the coordinate system every layout, every measurement and
	/// every hit test works in; the projection that turns it into clip space is
	/// built once per frame from the engine's NATIVE maths
	/// (<see cref="NativeMath.BuildOrthographicPixelSpace"/>).
	/// </summary>
	public readonly struct UiRect
	{
		public readonly float X;
		public readonly float Y;
		public readonly float Width;
		public readonly float Height;

		public UiRect(float x, float y, float width, float height)
		{
			X = x;
			Y = y;
			Width = width;
			Height = height;
		}

		public float Left => X;
		public float Top => Y;
		public float Right => X + Width;
		public float Bottom => Y + Height;

		public Vector2 Min => new Vector2(X, Y);
		public Vector2 Max => new Vector2(X + Width, Y + Height);
		public Vector2 Center => new Vector2(X + (Width * 0.5f), Y + (Height * 0.5f));

		public bool Contains(Vector2 point) =>
			point.X >= X && point.X < X + Width && point.Y >= Y && point.Y < Y + Height;

		/// <summary>The same rectangle moved by an offset - how a child is placed.</summary>
		public UiRect OffsetBy(Vector2 offset) => new UiRect(X + offset.X, Y + offset.Y, Width, Height);

		/// <summary>The same rectangle with a margin removed from every side.</summary>
		public UiRect Deflate(float margin) =>
			new UiRect(X + margin, Y + margin, MathF.Max(0.0f, Width - (margin * 2.0f)), MathF.Max(0.0f, Height - (margin * 2.0f)));

		public override string ToString() =>
			"UiRect(" + X + ", " + Y + ", " + Width + ", " + Height + ")";
	}

	/// <summary>
	/// One UI vertex. The position is already in CLIP SPACE: a UI layout
	/// transforms its pixel-space corners with the frame's pixel-space
	/// projection on the CPU, so the shader has no matrix to multiply and the
	/// whole interface is one buffer upload.
	///
	/// <c>Uv</c> addresses the texture the draw samples, <c>Color</c> is the
	/// per-vertex colour the fragment stage multiplies by the texture's coverage.
	/// The layout matches the UiVertexInput struct of Assembly/Shaders/UiQuad.
	/// </summary>
	[StructLayout(LayoutKind.Sequential, Pack = 4)]
	public struct UiVertex
	{
		public Vector2 Position;
		public Vector2 Uv;
		public Vector4 Color;

		public UiVertex(Vector2 position, Vector2 uv, Vector4 color)
		{
			Position = position;
			Uv = uv;
			Color = color;
		}

		/// <summary>Bytes one vertex occupies in the vertex buffer.</summary>
		public const uint Stride = 32;
	}

	/// <summary>
	/// One draw call of the UI: a run of indices that share a texture. The
	/// draw list merges consecutive quads that sample the same texture, so a
	/// screen full of flat panels and one font costs a handful of draws.
	/// </summary>
	public readonly struct UiDrawCommand
	{
		public readonly int FirstIndex;
		public readonly int IndexCount;
		public readonly int TextureBindlessSlot;

		public UiDrawCommand(int firstIndex, int indexCount, int textureBindlessSlot)
		{
			FirstIndex = firstIndex;
			IndexCount = indexCount;
			TextureBindlessSlot = textureBindlessSlot;
		}
	}

	/// <summary>
	/// The quads one UI frame is made of, in the order they were emitted. A
	/// layout appends rectangles and glyphs; the renderer uploads the vertex and
	/// index arrays once and replays the commands.
	///
	/// The list is reused between frames (the renderer calls
	/// <see cref="Clear"/>), so building a frame allocates nothing once the
	/// buffers have grown to the size the interface needs.
	/// </summary>
	public sealed class UiDrawList
	{
		private readonly List<UiVertex> vertices = new List<UiVertex>(1024);
		private readonly List<uint> indices = new List<uint>(1536);
		private readonly List<UiDrawCommand> commands = new List<UiDrawCommand>(16);

		public IReadOnlyList<UiVertex> Vertices => vertices;
		public IReadOnlyList<uint> Indices => indices;
		public IReadOnlyList<UiDrawCommand> Commands => commands;

		public int QuadCount => vertices.Count / 4;

		public void Clear()
		{
			vertices.Clear();
			indices.Clear();
			commands.Clear();
		}

		/// <summary>
		/// Appends one quad. <paramref name="min"/> and <paramref name="max"/> are
		/// the corners in PIXEL space; <paramref name="projection"/> carries them
		/// into clip space. The four corners keep the colours given so a quad can
		/// carry a gradient without a second draw.
		/// </summary>
		public void AddQuad(
			Vector2 min,
			Vector2 max,
			Vector2 uvMin,
			Vector2 uvMax,
			Vector4 topLeftColor,
			Vector4 topRightColor,
			Vector4 bottomRightColor,
			Vector4 bottomLeftColor,
			int textureBindlessSlot,
			Matrix4x4 projection)
		{
			int firstIndex = indices.Count;

			// A run of quads that sample the same texture stays one draw.
			if (commands.Count > 0)
			{
				UiDrawCommand lastCommand = commands[commands.Count - 1];
				if (lastCommand.TextureBindlessSlot == textureBindlessSlot &&
					lastCommand.FirstIndex + lastCommand.IndexCount == firstIndex)
				{
					commands[commands.Count - 1] = new UiDrawCommand(
						lastCommand.FirstIndex, lastCommand.IndexCount + 6, textureBindlessSlot);
				}
				else
				{
					commands.Add(new UiDrawCommand(firstIndex, 6, textureBindlessSlot));
				}
			}
			else
			{
				commands.Add(new UiDrawCommand(firstIndex, 6, textureBindlessSlot));
			}

			uint baseVertex = (uint)vertices.Count;
			vertices.Add(new UiVertex(Project(min.X, min.Y, projection), new Vector2(uvMin.X, uvMin.Y), topLeftColor));
			vertices.Add(new UiVertex(Project(max.X, min.Y, projection), new Vector2(uvMax.X, uvMin.Y), topRightColor));
			vertices.Add(new UiVertex(Project(max.X, max.Y, projection), new Vector2(uvMax.X, uvMax.Y), bottomRightColor));
			vertices.Add(new UiVertex(Project(min.X, max.Y, projection), new Vector2(uvMin.X, uvMax.Y), bottomLeftColor));

			// Two triangles, wound so the quad is visible from either side: the
			// UI pipeline culls nothing, which keeps a mirrored layout working.
			indices.Add(baseVertex + 0);
			indices.Add(baseVertex + 1);
			indices.Add(baseVertex + 2);
			indices.Add(baseVertex + 0);
			indices.Add(baseVertex + 2);
			indices.Add(baseVertex + 3);
		}

		/// <summary>Appends a flat-coloured quad - a panel, a bar, a border.</summary>
		public void AddRectangle(UiRect rectangle, Vector4 color, int textureBindlessSlot, Matrix4x4 projection)
		{
			AddQuad(rectangle.Min, rectangle.Max, Vector2.Zero, Vector2.One, color, color, color, color, textureBindlessSlot, projection);
		}

		/// <summary>
		/// Appends a run of glyph quads. <paramref name="glyphQuads"/> is the
		/// native layout (8 floats per glyph: left, top, right, bottom, u0, v0,
		/// u1, v1) and <paramref name="origin"/> is where the text's top-left
		/// corner sits in pixel space.
		/// </summary>
		public void AddGlyphs(
			ReadOnlySpan<float> glyphQuads,
			Vector2 origin,
			Vector4 color,
			int textureBindlessSlot,
			Matrix4x4 projection)
		{
			const int QuadFloatCount = 8;
			int quadCount = glyphQuads.Length / QuadFloatCount;
			for (int quadIndex = 0; quadIndex < quadCount; ++quadIndex)
			{
				int offset = quadIndex * QuadFloatCount;
				float left = glyphQuads[offset + 0] + origin.X;
				float top = glyphQuads[offset + 1] + origin.Y;
				float right = glyphQuads[offset + 2] + origin.X;
				float bottom = glyphQuads[offset + 3] + origin.Y;

				// A space carries an empty rectangle on purpose; skipping it here
				// keeps the vertex buffer free of degenerate quads.
				if (right <= left || bottom <= top)
				{
					continue;
				}

				AddQuad(
					new Vector2(left, top),
					new Vector2(right, bottom),
					new Vector2(glyphQuads[offset + 4], glyphQuads[offset + 5]),
					new Vector2(glyphQuads[offset + 6], glyphQuads[offset + 7]),
					color, color, color, color,
					textureBindlessSlot,
					projection);
			}
		}

		/// <summary>
		/// A pixel-space position taken through the frame's pixel-space
		/// projection, i.e. into the clip space the vertex buffer stores.
		///
		/// <see cref="Matrix4x4"/> transforms a COLUMN vector (result = M * v), so
		/// the first row - M11, M12, M14 - produces the x and the second row the y.
		/// The translation of the pixel-space projection lives in the fourth
		/// COLUMN (M14 = M24 = -1), which is what puts the top-left pixel at the
		/// clip-space corner instead of at the centre.
		/// </summary>
		private static Vector2 Project(float x, float y, Matrix4x4 projection)
		{
			return new Vector2(
				(x * projection.M11) + (y * projection.M12) + projection.M14,
				(x * projection.M21) + (y * projection.M22) + projection.M24);
		}
	}

	/// <summary>
	/// The pointer state a UI frame reacts to, gathered from
	/// <see cref="Input"/> by the canvas so an element never talks to the input
	/// system itself.
	/// </summary>
	public readonly struct UiInputState
	{
		/// <summary>Pointer position in UI pixel space.</summary>
		public readonly Vector2 MousePosition;

		/// <summary>True while the primary button is held.</summary>
		public readonly bool IsMouseButtonDown;

		/// <summary>True only on the frame the primary button went down.</summary>
		public readonly bool WasMouseButtonPressed;

		/// <summary>True only on the frame the primary button was released.</summary>
		public readonly bool WasMouseButtonReleased;

		public UiInputState(Vector2 mousePosition, bool isMouseButtonDown, bool wasMouseButtonPressed, bool wasMouseButtonReleased)
		{
			MousePosition = mousePosition;
			IsMouseButtonDown = isMouseButtonDown;
			WasMouseButtonPressed = wasMouseButtonPressed;
			WasMouseButtonReleased = wasMouseButtonReleased;
		}

		/// <summary>Reads the current state from the engine's input facade.</summary>
		public static UiInputState Capture()
		{
			return new UiInputState(
				Input.MousePosition,
				Input.GetMouseButton(0),
				Input.GetMouseButtonDown(0),
				Input.GetMouseButtonUp(0));
		}
	}
}

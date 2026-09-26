using System.Numerics;
using VspEngine.Rendering;

namespace VspEngine.UI
{
	/// <summary>
	/// A filled rectangle: the flat style's only shape.
	///
	/// An image with no texture is a solid panel or bar, which is what most of a
	/// minimal interface is made of. Given a <see cref="Texture"/> it samples
	/// that texture's coverage and multiplies it by <see cref="Color"/>, which is
	/// how an icon or a glyph atlas becomes a widget.
	///
	/// <see cref="BorderColor"/> with a non-zero alpha draws a hairline inside
	/// the rectangle - one more quad, no shader work.
	/// </summary>
	public class Image : UiElement
	{
		public Image()
		{
			Name = "Image";
			Color = new Color(1.0f, 1.0f, 1.0f, 1.0f);
			BorderColor = new Color(0.0f, 0.0f, 0.0f, 0.0f);
		}

		/// <summary>Tint multiplied into the texture (or the fill colour).</summary>
		public Color Color { get; set; }

		/// <summary>Hairline drawn inside the rectangle; alpha 0 draws none.</summary>
		public Color BorderColor { get; set; }

		/// <summary>Thickness of the hairline in pixels.</summary>
		public float BorderThickness { get; set; } = 1.0f;

		/// <summary>Texture to sample, or null for a solid fill.</summary>
		public Texture2D? Texture { get; set; }

		/// <summary>Region of the texture to sample, in normalized coordinates.</summary>
		public Vector2 UvMin { get; set; } = Vector2.Zero;

		public Vector2 UvMax { get; set; } = Vector2.One;

		protected override void OnDraw(UiDrawList drawList, UiTheme theme, Matrix4x4 projection)
		{
			if (WidthOrHeightIsEmpty())
			{
				return;
			}

			int textureBindlessSlot = Texture?.BindlessSlot ?? OwningCanvas?.SolidTextureBindlessSlot ?? -1;
			if (textureBindlessSlot < 0)
			{
				// No texture to sample and no solid-colour texture either: there
				// is nothing a draw could read, so the element stays invisible
				// rather than drawing undefined pixels.
				return;
			}

			Vector4 color = UiTheme.ToVector(Color);
			drawList.AddQuad(
				AbsoluteBounds.Min, AbsoluteBounds.Max,
				UvMin, UvMax,
				color, color, color, color,
				textureBindlessSlot, projection);

			if (BorderColor.A > 0.0f && BorderThickness > 0.0f)
			{
				Vector4 borderColor = UiTheme.ToVector(BorderColor);
				int solidSlot = OwningCanvas?.SolidTextureBindlessSlot ?? -1;
				if (solidSlot < 0)
				{
					return;
				}

				float thickness = BorderThickness;
				UiRect bounds = AbsoluteBounds;

				// Four hairlines: top, bottom, left, right.
				drawList.AddRectangle(new UiRect(bounds.Left, bounds.Top, bounds.Width, thickness), borderColor, solidSlot, projection);
				drawList.AddRectangle(new UiRect(bounds.Left, bounds.Bottom - thickness, bounds.Width, thickness), borderColor, solidSlot, projection);
				drawList.AddRectangle(new UiRect(bounds.Left, bounds.Top + thickness, thickness, bounds.Height - (thickness * 2.0f)), borderColor, solidSlot, projection);
				drawList.AddRectangle(new UiRect(bounds.Right - thickness, bounds.Top + thickness, thickness, bounds.Height - (thickness * 2.0f)), borderColor, solidSlot, projection);
			}
		}

		private bool WidthOrHeightIsEmpty() => AbsoluteBounds.Width <= 0.0f || AbsoluteBounds.Height <= 0.0f;
	}
}

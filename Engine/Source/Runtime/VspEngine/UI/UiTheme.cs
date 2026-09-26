using System.Numerics;
using VspEngine.Rendering;

namespace VspEngine.UI
{
	/// <summary>
	/// The look of the interface: one place that names every colour and every
	/// measurement a widget draws itself with.
	///
	/// The style is FLAT and MINIMAL - solid fills, no gradients, no shadows, no
	/// rounded corners, one restrained accent colour, a single hairline border
	/// and generous spacing. Widgets read the theme instead of carrying literals,
	/// so a different look is a different <see cref="UiTheme"/>, not a rewrite.
	///
	/// The defaults are a dark, low-contrast interface: a panel that sits behind
	/// the content rather than competing with it, a surface that lifts a control
	/// off the panel, and one blue accent for the thing the pointer is on.
	/// </summary>
	public sealed class UiTheme
	{
		// -------- Surfaces --------
		/// <summary>Backdrop of a full-screen canvas.</summary>
		public Color CanvasColor = new Color(0.05f, 0.06f, 0.08f, 0.0f);

		/// <summary>A panel or card: the container a control sits on.</summary>
		public Color PanelColor = new Color(0.11f, 0.12f, 0.15f, 0.92f);

		/// <summary>A control at rest.</summary>
		public Color SurfaceColor = new Color(0.16f, 0.17f, 0.21f, 1.0f);

		/// <summary>A control the pointer is over.</summary>
		public Color SurfaceHoverColor = new Color(0.21f, 0.23f, 0.28f, 1.0f);

		/// <summary>A control being pressed.</summary>
		public Color SurfacePressedColor = new Color(0.10f, 0.11f, 0.14f, 1.0f);

		/// <summary>A control that cannot be used.</summary>
		public Color SurfaceDisabledColor = new Color(0.13f, 0.13f, 0.15f, 1.0f);

		// -------- Accent --------
		/// <summary>The one accent colour: what the interface wants the eye on.</summary>
		public Color AccentColor = new Color(0.26f, 0.56f, 0.98f, 1.0f);

		/// <summary>A 1-pixel accent line under a pressed control.</summary>
		public Color AccentMutedColor = new Color(0.26f, 0.56f, 0.98f, 0.45f);

		// -------- Lines --------
		/// <summary>Hairline border of a panel or a control.</summary>
		public Color BorderColor = new Color(1.0f, 1.0f, 1.0f, 0.10f);

		/// <summary>Divider between two rows.</summary>
		public Color DividerColor = new Color(1.0f, 1.0f, 1.0f, 0.06f);

		// -------- Text --------
		public Color TextColor = new Color(0.92f, 0.94f, 0.97f, 1.0f);
		public Color TextMutedColor = new Color(0.62f, 0.66f, 0.72f, 1.0f);
		public Color TextOnAccentColor = new Color(1.0f, 1.0f, 1.0f, 1.0f);
		public Color TextDisabledColor = new Color(0.45f, 0.47f, 0.52f, 1.0f);

		// -------- Measurements (pixels) --------
		/// <summary>Height of a standard control.</summary>
		public float ControlHeight = 34.0f;

		/// <summary>Inner horizontal padding of a control - the flat style's air.</summary>
		public float ControlPaddingX = 14.0f;

		/// <summary>Inner vertical padding of a control.</summary>
		public float ControlPaddingY = 8.0f;

		/// <summary>Gap between two stacked elements.</summary>
		public float Spacing = 8.0f;

		/// <summary>Padding between a panel's edge and its content.</summary>
		public float PanelPadding = 14.0f;

		/// <summary>Thickness of a border or a divider, in pixels.</summary>
		public float LineThickness = 1.0f;

		/// <summary>
		/// How much the pointer must move before a press turns into a drag
		/// instead of a click.
		/// </summary>
		public float ClickDragThreshold = 6.0f;

		/// <summary>The flat default: dark, quiet, one accent.</summary>
		public static UiTheme CreateDefault() => new UiTheme();

		/// <summary>Converts a theme colour into the vector a vertex carries.</summary>
		public static Vector4 ToVector(Color color) => new Vector4(color.R, color.G, color.B, color.A);
	}
}

using System;
using System.Numerics;
using VspEngine.Rendering;

namespace VspEngine.UI
{
	/// <summary>
	/// A flat push button: a filled rectangle, an optional hairline border, an
	/// accent bar that marks the press, and a centred label.
	///
	/// The state machine is the whole widget - rest, hover, press, release - and
	/// it follows the usual convention: the click fires on RELEASE while the
	/// pointer is still inside, and a press that wanders further than
	/// <see cref="UiTheme.ClickDragThreshold"/> before release is treated as a
	/// drag and produces no click. That is what keeps a button usable next to a
	/// camera rig that also reads the mouse.
	///
	/// The label is a child <see cref="Text"/>, so it goes through the engine's
	/// localisation service like every other string, and it can be replaced by
	/// any widget through <see cref="AddChild"/>.
	/// </summary>
	public class Button : UiElement
	{
		private bool isPressed;
		private Vector2 pressStartPosition;

		public Button()
			: this(string.Empty)
		{
		}

		public Button(string translationKey)
		{
			Name = "Button";

			// The label is built first: the TranslationKey property below forwards
			// to it, so it has to exist before the key is set.
			Label = new Text
			{
				Name = "ButtonLabel",
				Alignment = UiTextAlignment.Center,
				VerticalAlignment = UiTextVerticalAlignment.Middle,
			};
			AddChild(Label);

			TranslationKey = translationKey;
		}

		/// <summary>Raised on release, while the pointer is still over the button.</summary>
		public event Action<Button>? Clicked;

		/// <summary>Key the label is translated from.</summary>
		public string TranslationKey
		{
			get => Label.TranslationKey;
			set => Label.TranslationKey = value;
		}

		/// <summary>Text that is not translated; used when no key is set.</summary>
		public string Literal
		{
			get => Label.Literal;
			set => Label.Literal = value;
		}

		/// <summary>The label widget, for colours and sizes a theme does not name.</summary>
		public Text Label { get; }

		/// <summary>Colour of the fill; null follows the theme's state colours.</summary>
		public Color? BackgroundOverride { get; set; }

		/// <summary>Draws the hairline border (the flat style uses one).</summary>
		public bool ShowBorder { get; set; } = true;

		/// <summary>Draws the accent bar under a pressed button.</summary>
		public bool ShowPressAccent { get; set; } = true;

		/// <summary>True while the pointer is down on this button.</summary>
		public bool IsPressed => isPressed;

		/// <summary>
		/// Fires the click as if the button had been released under the pointer.
		/// A test or a keyboard shortcut calls this.
		/// </summary>
		public void Invoke()
		{
			if (!IsEnabled)
			{
				return;
			}
			Clicked?.Invoke(this);
		}

		protected override void OnUpdate(UiInputState input, UiTheme theme)
		{
			// The label fills the button. Its bounds are relative to the button,
			// so this is what makes the centred alignment below mean "centred in
			// the button" rather than "centred in nothing".
			Label.Bounds = new UiRect(0.0f, 0.0f, Bounds.Width, Bounds.Height);

			bool isHoveredNow = IsHovered;

			if (!IsEnabled)
			{
				isPressed = false;
				return;
			}

			if (isHoveredNow && input.WasMouseButtonPressed)
			{
				isPressed = true;
				pressStartPosition = input.MousePosition;
			}

			if (!isPressed)
			{
				return;
			}

			// A press that moved too far became a drag: the button gives up.
			if (Vector2.Distance(input.MousePosition, pressStartPosition) > theme.ClickDragThreshold)
			{
				isPressed = false;
				return;
			}

			if (!input.WasMouseButtonReleased)
			{
				return;
			}

			isPressed = false;
			if (isHoveredNow)
			{
				Clicked?.Invoke(this);
			}
		}

		protected override void OnDraw(UiDrawList drawList, UiTheme theme, Matrix4x4 projection)
		{
			if (AbsoluteBounds.Width <= 0.0f || AbsoluteBounds.Height <= 0.0f)
			{
				return;
			}

			int solidSlot = OwningCanvas?.SolidTextureBindlessSlot ?? -1;
			if (solidSlot < 0)
			{
				return;
			}

			Color fillColor = ResolveFillColor(theme);
			drawList.AddRectangle(AbsoluteBounds, UiTheme.ToVector(fillColor), solidSlot, projection);

			if (ShowBorder)
			{
				Vector4 borderColor = UiTheme.ToVector(theme.BorderColor);
				UiRect bounds = AbsoluteBounds;
				float thickness = theme.LineThickness;
				drawList.AddRectangle(new UiRect(bounds.Left, bounds.Top, bounds.Width, thickness), borderColor, solidSlot, projection);
				drawList.AddRectangle(new UiRect(bounds.Left, bounds.Bottom - thickness, bounds.Width, thickness), borderColor, solidSlot, projection);
				drawList.AddRectangle(new UiRect(bounds.Left, bounds.Top, thickness, bounds.Height), borderColor, solidSlot, projection);
				drawList.AddRectangle(new UiRect(bounds.Right - thickness, bounds.Top, thickness, bounds.Height), borderColor, solidSlot, projection);
			}

			if (ShowPressAccent && isPressed)
			{
				UiRect bounds = AbsoluteBounds;
				float accentHeight = 2.0f;
				drawList.AddRectangle(
					new UiRect(bounds.Left, bounds.Bottom - accentHeight, bounds.Width, accentHeight),
					UiTheme.ToVector(theme.AccentColor),
					solidSlot,
					projection);
			}
		}

		/// <summary>
		/// The fill this frame shows: the override when one was set, otherwise the
		/// theme colour of the current state.
		/// </summary>
		private Color ResolveFillColor(UiTheme theme)
		{
			if (BackgroundOverride.HasValue)
			{
				return BackgroundOverride.Value;
			}

			if (!IsEnabled)
			{
				return theme.SurfaceDisabledColor;
			}
			if (isPressed)
			{
				return theme.SurfacePressedColor;
			}
			if (IsHovered)
			{
				return theme.SurfaceHoverColor;
			}
			return theme.SurfaceColor;
		}
	}
}

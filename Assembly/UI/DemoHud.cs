using System;
using System.Globalization;
using System.Numerics;

using VspEngine;
using VspEngine.UI;

namespace Assembly.UI
{
	/// <summary>
	/// The demo's interface: a flat, minimal heads-up display built out of the
	/// engine's UI framework (<see cref="Canvas"/>, <see cref="Image"/>,
	/// <see cref="Text"/>, <see cref="Button"/>).
	///
	/// It is deliberately a demo of every widget rather than a game's real HUD:
	///
	///   * a title, a status line and a footer, all through the localisation
	///     service, so every static string in the demo is a KEY
	///     (<c>demo.title</c>, ...) that a locale file translates;
	///   * a column of buttons that drive the cube - spin direction, cube reset,
	///     camera reset - which is what exercises the button state machine;
	///   * a panel, an accent bar and a divider, which is what the flat style's
	///     surfaces are made of.
	///
	/// The HUD owns the canvas but no graphics resources: it hands the canvas to
	/// the <c>UiRenderer</c>, which owns the shader, the buffers and the font
	/// atlases.
	/// </summary>
	public sealed class DemoHud : IDisposable
	{
		public const string TitleKey = "demo.title";
		public const string StatusKey = "demo.status";
		public const string SpinKey = "demo.button.spin";
		public const string ResetKey = "demo.button.reset";
		public const string CameraKey = "demo.button.camera";
		public const string HintKey = "demo.hint";
		public const string SpinClockwiseKey = "demo.spin.clockwise";
		public const string SpinCounterClockwiseKey = "demo.spin.counterClockwise";

		private const float PanelWidth = 330.0f;

		private readonly Canvas canvas;
		private readonly Image panel;
		private readonly Text titleText;
		private readonly Text statusText;
		private readonly Text hintText;
		private readonly Image pointerMarker;
		private readonly Button spinButton;

		/// <summary>Raised when the spin button asks for a direction change.</summary>
		public event Action? SpinToggleRequested;

		/// <summary>Raised when the reset button asks for the cube to go home.</summary>
		public event Action? ResetRequested;

		/// <summary>Raised when the camera button asks for the view to be reset.</summary>
		public event Action? CameraResetRequested;

		public DemoHud()
		{
			canvas = new Canvas(UiTheme.CreateDefault());
			canvas.DefaultTextPixelSize = 18.0f;

			// The panel the whole interface sits on, pinned to the top-left
			// corner with the style's standard padding.
			panel = new Image
			{
				Name = "HudPanel",
				Bounds = new UiRect(18.0f, 18.0f, PanelWidth, 214.0f),
				Color = canvas.Theme.PanelColor,
				BorderColor = canvas.Theme.BorderColor,
			};
			canvas.AddChild(panel);

			// A 4-pixel accent bar along the panel's top edge: the flat style's
			// way of saying "this is the interface" without a shadow or a corner.
			panel.AddChild(new Image
			{
				Name = "AccentBar",
				Bounds = new UiRect(0.0f, 0.0f, PanelWidth, 4.0f),
				Color = canvas.Theme.AccentColor,
			});

			titleText = new Text
			{
				Name = "Title",
				TranslationKey = TitleKey,
				Bounds = new UiRect(16.0f, 16.0f, PanelWidth - 32.0f, 26.0f),
				PixelSize = 22.0f,
				Color = canvas.Theme.TextColor,
				VerticalAlignment = UiTextVerticalAlignment.Middle,
			};
			panel.AddChild(titleText);

			statusText = new Text
			{
				Name = "Status",
				Bounds = new UiRect(16.0f, 46.0f, PanelWidth - 32.0f, 22.0f),
				PixelSize = 16.0f,
				Color = canvas.Theme.TextMutedColor,
				VerticalAlignment = UiTextVerticalAlignment.Middle,
			};
			panel.AddChild(statusText);

			panel.AddChild(new Image
			{
				Name = "Divider",
				Bounds = new UiRect(16.0f, 74.0f, PanelWidth - 32.0f, 1.0f),
				Color = canvas.Theme.DividerColor,
			});

			// Three buttons in one column. The first one's label follows the spin
			// state, which is the whole point of a HUD button.
			spinButton = panel.AddChild(CreateButton(panel, "SpinButton", SpinKey, 84.0f));
			spinButton.Clicked += OnSpinClicked;

			Button resetButton = panel.AddChild(CreateButton(panel, "ResetButton", ResetKey, 126.0f));
			resetButton.Clicked += OnResetClicked;

			Button cameraButton = panel.AddChild(CreateButton(panel, "CameraButton", CameraKey, 168.0f));
			cameraButton.Clicked += OnCameraClicked;

			// The footer is pinned to the bottom-left of the canvas: it is not
			// part of the panel's content.
			hintText = new Text
			{
				Name = "Hint",
				Bounds = new UiRect(20.0f, 0.0f, 900.0f, 30.0f),
				PixelSize = 15.0f,
				Color = canvas.Theme.TextMutedColor,
				VerticalAlignment = UiTextVerticalAlignment.Bottom,
			};
			canvas.AddChild(hintText);

			// A marker that follows the pointer, drawn as the last child so it is
			// on top of everything: the flat style has no cursor of its own, and
			// a screenshot has to show where the pointer was.
			pointerMarker = new Image
			{
				Name = "PointerMarker",
				Bounds = new UiRect(0.0f, 0.0f, 12.0f, 12.0f),
				Color = canvas.Theme.AccentColor,
			};
			canvas.AddChild(pointerMarker);
		}

		/// <summary>The tree the UI renderer draws.</summary>
		public Canvas Canvas => canvas;

		/// <summary>
		/// Advances the interface by one frame. The pipeline calls it before it
		/// draws, so hover state, presses and clicks are all resolved here.
		/// </summary>
		public void Update(UiInputState input, int width, int height)
		{
			// The footer follows the bottom edge of whatever the window is.
			hintText.Bounds = new UiRect(20.0f, height - 40.0f, width - 40.0f, 24.0f);

			// Placed right before the tree is walked, so it reports where the
			// pointer is THIS frame.
			pointerMarker.Bounds = new UiRect(input.MousePosition.X - 6.0f, input.MousePosition.Y - 6.0f, 12.0f, 12.0f);

			canvas.Update(input, width, height);
		}

		/// <summary>
		/// Reports where the cube is and which way it spins. Both lines are
		/// translated TEMPLATES with placeholders, which is what keeps word order
		/// a property of the translation rather than of this code.
		/// </summary>
		public void ReportCubeState(Vector3 position, bool isCounterClockwise)
		{
			string spinDirection = I18N.Translate(isCounterClockwise ? SpinCounterClockwiseKey : SpinClockwiseKey);
			SetLiveText(statusText, I18N.Translate(
				StatusKey, FormatFloat(position.X), FormatFloat(position.Y), FormatFloat(position.Z), spinDirection));

			SetLiveText(spinButton.Label, spinDirection);
		}

		/// <summary>Reports which way the camera is currently looking.</summary>
		public void ReportCameraState(float yawDegrees, float pitchDegrees)
		{
			SetLiveText(hintText, I18N.Translate(HintKey, FormatFloat(yawDegrees), FormatFloat(pitchDegrees)));
		}

		public void Dispose()
		{
			canvas.ClearChildren();
		}

		/// <summary>
		/// Replaces a text element's content with a string that is already
		/// resolved, so it is not translated a second time.
		/// </summary>
		private static void SetLiveText(Text text, string resolvedText)
		{
			text.TranslationKey = string.Empty;
			text.Literal = resolvedText;
		}

		private Button CreateButton(UiElement parent, string name, string translationKey, float y)
		{
			Button button = new Button(translationKey)
			{
				Name = name,
				Bounds = new UiRect(canvas.Theme.PanelPadding + 2.0f, y, PanelWidth - 36.0f, canvas.Theme.ControlHeight),
			};
			button.Label.Color = canvas.Theme.TextColor;
			button.Label.PixelSize = 16.0f;
			return button;
		}

		private void OnSpinClicked(Button button) => SpinToggleRequested?.Invoke();

		private void OnResetClicked(Button button) => ResetRequested?.Invoke();

		private void OnCameraClicked(Button button) => CameraResetRequested?.Invoke();

		private static string FormatFloat(float value) =>
			value.ToString("0.0", CultureInfo.InvariantCulture);
	}
}

using System;
using System.Globalization;
using System.Numerics;

using VspEngine;
using VspEngine.Rendering;
using VspEngine.UI;

namespace Assembly.UI
{
	/// <summary>
	/// The demo's interface, built out of the engine's two layout sources - which
	/// is what the demo is for, so both are used for what they are good at:
	///
	///   * the PANEL, its accent bar, its divider, its title and its three buttons
	///     are described by a JSON document (<c>Assembly/Ui/DemoHud.json</c>,
	///     staged next to the executable as <c>Ui/DemoHud.json</c>) and laid out by
	///     the engine (<see cref="UiSystem.LoadLayoutFromJson"/>). Structure and
	///     style are data: the layout names its elements, their rectangles, the
	///     theme colour each one draws with and the localisation KEY a static label
	///     shows;
	///   * the LIVE half - the status line, the camera hint and the pointer marker,
	///     whose text, position and visibility change every frame - is built in
	///     CODE on a canvas of its own (<see cref="LiveCanvas"/>) and published to
	///     the engine. Behaviour is code, and it binds onto the layout by the names
	///     the document gives (<see cref="UiSystem.FindElement(string)"/>).
	///
	/// Neither half draws itself: the engine draws the layout canvas and the live
	/// canvas in every frame it opens, in that order, through its own UI renderer.
	/// </summary>
	public sealed class DemoHud : IDisposable
	{
		/// <summary>The layout document the demo ships, next to the executable's Ui folder.</summary>
		public const string LayoutFileName = "DemoHud.json";

		public const string TitleKey = "demo.title";
		public const string StatusKey = "demo.status";
		public const string SpinKey = "demo.button.spin";
		public const string ResetKey = "demo.button.reset";
		public const string CameraKey = "demo.button.camera";
		public const string HintKey = "demo.hint";
		public const string SpinClockwiseKey = "demo.spin.clockwise";
		public const string SpinCounterClockwiseKey = "demo.spin.counterClockwise";

		// Names the LAYOUT document gives its elements: this is the whole contract
		// between the data and the code of the interface.
		private const string SpinButtonName = "SpinButton";
		private const string ResetButtonName = "ResetButton";
		private const string CameraButtonName = "CameraButton";

		private const float PanelWidth = 330.0f;
		private const float PanelMargin = 18.0f;

		private readonly Canvas liveCanvas;
		private readonly Text statusText;
		private readonly Text hintText;
		private readonly Image pointerMarker;

		private Button? spinButton;

		/// <summary>Raised when the spin button asks for a direction change.</summary>
		public event Action? SpinToggleRequested;

		/// <summary>Raised when the reset button asks for the cube to go home.</summary>
		public event Action? ResetRequested;

		/// <summary>Raised when the camera button asks for the view to be reset.</summary>
		public event Action? CameraResetRequested;

		public DemoHud()
		{
			liveCanvas = new Canvas(UiTheme.CreateDefault())
			{
				Name = "DemoHudLive",
			};
			liveCanvas.DefaultTextPixelSize = 18.0f;

			// The status line sits inside the panel the LAYOUT draws, so its
			// rectangle is stated in canvas pixels: panel margin + inner padding.
			statusText = new Text
			{
				Name = "Status",
				Bounds = new UiRect(PanelMargin + 16.0f, PanelMargin + 46.0f, PanelWidth - 32.0f, 22.0f),
				PixelSize = 16.0f,
				Color = liveCanvas.Theme.TextMutedColor,
				VerticalAlignment = UiTextVerticalAlignment.Middle,
			};
			liveCanvas.AddChild(statusText);

			// The footer is pinned to the bottom-left of the canvas: it is not part
			// of the panel's content, so it belongs to the live canvas.
			hintText = new Text
			{
				Name = "Hint",
				Bounds = new UiRect(20.0f, 0.0f, 900.0f, 30.0f),
				PixelSize = 15.0f,
				Color = liveCanvas.Theme.TextMutedColor,
				VerticalAlignment = UiTextVerticalAlignment.Bottom,
			};
			liveCanvas.AddChild(hintText);

			// A marker that follows the pointer, drawn as the last child so it is
			// on top of everything: the flat style has no cursor of its own, and a
			// screenshot has to show where the pointer was.
			pointerMarker = new Image
			{
				Name = "PointerMarker",
				Bounds = new UiRect(0.0f, 0.0f, 12.0f, 12.0f),
				Color = liveCanvas.Theme.AccentColor,
			};
			liveCanvas.AddChild(pointerMarker);
		}

		/// <summary>
		/// The canvas the demo builds in CODE: it holds what changes every frame,
		/// and the engine draws it on top of the JSON layout.
		/// </summary>
		public Canvas LiveCanvas => liveCanvas;

		/// <summary>
		/// Asks the engine for the JSON layout and binds this HUD to it: the three
		/// buttons of the panel are found by name, so their clicks reach the demo
		/// without the demo having built them.
		/// </summary>
		public bool LoadLayout()
		{
			if (!UiSystem.LoadLayoutFromJson(LayoutFileName))
			{
				return false;
			}

			spinButton = UiSystem.FindElement<Button>(SpinButtonName);
			Button? resetButton = UiSystem.FindElement<Button>(ResetButtonName);
			Button? cameraButton = UiSystem.FindElement<Button>(CameraButtonName);
			if (spinButton == null || resetButton == null || cameraButton == null)
			{
				Debug.LogError("DemoHud: the layout '" + LayoutFileName + "' is missing one of its buttons ("
					+ SpinButtonName + ", " + ResetButtonName + ", " + CameraButtonName + ").");
				return false;
			}

			spinButton.Clicked += OnSpinClicked;
			resetButton.Clicked += OnResetClicked;
			cameraButton.Clicked += OnCameraClicked;
			return true;
		}

		/// <summary>
		/// Places the live widgets for THIS frame's render target size. The engine
		/// updates and draws both canvases itself, with the pointer state of the
		/// frame, right before the frame is closed.
		/// </summary>
		public void UpdateFrame(int width, int height)
		{
			// The footer follows the bottom edge of whatever the window is.
			hintText.Bounds = new UiRect(20.0f, height - 40.0f, width - 40.0f, 24.0f);

			// Placed right before the canvases are drawn, so it reports where the
			// pointer is THIS frame. While the cursor is locked the pointer is
			// hidden and parked at the centre of the window, so the marker is put
			// away with it: a marker that only ever sits in the middle would say
			// nothing.
			pointerMarker.IsVisible = !Cursor.IsLocked;

			Vector2 pointerPosition = Input.MousePosition;
			pointerMarker.Bounds = new UiRect(
				pointerPosition.X - 6.0f, pointerPosition.Y - 6.0f, 12.0f, 12.0f);
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

			// The button's label is the layout's, but WHAT it says is the demo's:
			// the label is a text element like any other and code may rewrite it.
			if (spinButton != null)
			{
				SetLiveText(spinButton.Label, spinDirection);
			}
		}

		/// <summary>Reports which way the camera is currently looking.</summary>
		public void ReportCameraState(float yawDegrees, float pitchDegrees)
		{
			SetLiveText(hintText, I18N.Translate(HintKey, FormatFloat(yawDegrees), FormatFloat(pitchDegrees)));
		}

		public void Dispose()
		{
			// The live canvas is the demo's; the layout canvas belongs to the engine
			// and goes when the engine's UI does (VspEngine.UI.UiSystem.Release).
			liveCanvas.ClearChildren();
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

		private void OnSpinClicked(Button button) => SpinToggleRequested?.Invoke();

		private void OnResetClicked(Button button) => ResetRequested?.Invoke();

		private void OnCameraClicked(Button button) => CameraResetRequested?.Invoke();

		private static string FormatFloat(float value) =>
			value.ToString("0.0", CultureInfo.InvariantCulture);
	}
}

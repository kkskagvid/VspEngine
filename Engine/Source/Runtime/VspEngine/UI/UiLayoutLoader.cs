using System;
using System.Globalization;
using System.Numerics;
using VspEngine.Rendering;

namespace VspEngine.UI
{
	/// <summary>
	/// Builds a <see cref="Canvas"/> from a JSON DOCUMENT - the declarative half
	/// of the engine's UI layout, next to the code half.
	///
	/// The two halves have one job each, and this is what keeps them from
	/// drifting into each other:
	///
	///   * the JSON layout says WHAT THE INTERFACE IS: which elements exist, where
	///     they sit, how they look and which localisation key a static label
	///     shows. It is data, it is read once, and changing the interface is
	///     editing the file rather than recompiling the game;
	///   * code (<see cref="Canvas"/> and the widgets, built and updated per frame)
	///     says WHAT THE INTERFACE SHOWS NOW: a live readout, an element that
	///     appears only in some state, what a click does. It is behaviour, and it
	///     stays in the game.
	///
	/// The document shape:
	///
	///     {
	///       "canvas": { "defaultTextPixelSize": 18.0 },
	///       "theme":  { "PanelColor": "#1C1F26EB" },          // optional overrides
	///       "elements": [
	///         { "type": "Image", "name": "HudPanel", "rect": [18, 18, 330, 214],
	///           "color": "PanelColor", "borderColor": "BorderColor",
	///           "children": [
	///             { "type": "Text", "name": "Title", "rect": [16, 16, 298, 26],
	///               "key": "demo.title", "pixelSize": 22, "color": "TextColor",
	///               "verticalAlignment": "Middle" },
	///             { "type": "Button", "name": "SpinButton", "rect": [16, 84, 294, 34],
	///               "key": "demo.button.spin", "labelColor": "TextColor" }
	///           ] }
	///       ]
	///     }
	///
	/// A colour is either a THEME ROLE by name ("PanelColor", "AccentColor", ...)
	/// or a literal "#RRGGBB" / "#RRGGBBAA". A text is either a localisation "key"
	/// or a literal "text". Every field has a default, so a layout can state only
	/// what it cares about.
	///
	/// Nothing here throws and nothing fails the frame: a member that is missing
	/// or of the wrong type keeps its default, and an element that cannot be built
	/// is reported and skipped.
	/// </summary>
	public static class UiLayoutLoader
	{
		/// <summary>Builds the canvas a layout document describes.</summary>
		public static Canvas? BuildCanvas(JsonDocument document)
		{
			if (document == null || !document.IsValid)
			{
				Debug.LogError("UiLayoutLoader: no valid layout document was given.");
				return null;
			}

			JsonNode root = document.Root;
			if (!root.IsObject)
			{
				Debug.LogError("UiLayoutLoader: the layout document's root is not an object.");
				return null;
			}

			Canvas canvas = new Canvas(UiTheme.CreateDefault())
			{
				Name = root.GetMemberString("name", "Canvas"),
			};

			JsonNode canvasNode = root.GetMember("canvas");
			canvas.DefaultTextPixelSize = canvasNode.GetMemberFloat("defaultTextPixelSize", canvas.DefaultTextPixelSize);

			ApplyThemeOverrides(canvas.Theme, root.GetMember("theme"));

			JsonNode elements = root.GetMember("elements");
			if (!elements.IsArray)
			{
				Debug.LogError("UiLayoutLoader: the layout document has no 'elements' array.");
				return canvas;
			}

			int elementCount = 0;
			for (int elementIndex = 0; elementIndex < elements.ElementCount; ++elementIndex)
			{
				UiElement? element = BuildElement(canvas, elements.GetElement(elementIndex));
				if (element == null)
				{
					continue;
				}

				canvas.AddChild(element);
				++elementCount;
			}

			if (elementCount == 0)
			{
				Debug.LogError("UiLayoutLoader: none of the " + elements.ElementCount + " element(s) could be built.");
			}
			return canvas;
		}

		/// <summary>Builds one element and, recursively, everything it holds.</summary>
		private static UiElement? BuildElement(Canvas canvas, JsonNode elementNode)
		{
			if (!elementNode.IsObject)
			{
				Debug.LogError("UiLayoutLoader: an element is not a JSON object; it is skipped.");
				return null;
			}

			string elementType = elementNode.GetMemberString("type", "Image");
			string elementName = elementNode.GetMemberString("name", elementType);

			UiElement? element;
			if (elementType.Equals("Text", StringComparison.OrdinalIgnoreCase))
			{
				element = BuildText(canvas, elementNode);
			}
			else if (elementType.Equals("Button", StringComparison.OrdinalIgnoreCase))
			{
				element = BuildButton(canvas, elementNode);
			}
			else if (elementType.Equals("Image", StringComparison.OrdinalIgnoreCase))
			{
				element = BuildImage(canvas, elementNode);
			}
			else
			{
				Debug.LogError("UiLayoutLoader: the element '" + elementName + "' has the unknown type '"
					+ elementType + "'; it is skipped.");
				return null;
			}

			if (element == null)
			{
				return null;
			}

			element.Name = elementName;
			element.Bounds = ReadRect(elementNode, elementName);
			element.IsVisible = elementNode.GetMemberBool("visible", true);
			element.IsEnabled = elementNode.GetMemberBool("enabled", true);

			JsonNode children = elementNode.GetMember("children");
			for (int childIndex = 0; childIndex < children.ElementCount; ++childIndex)
			{
				UiElement? child = BuildElement(canvas, children.GetElement(childIndex));
				if (child != null)
				{
					element.AddChild(child);
				}
			}

			return element;
		}

		private static Image BuildImage(Canvas canvas, JsonNode elementNode)
		{
			return new Image
			{
				Color = ReadColor(canvas.Theme, elementNode, "color", canvas.Theme.TextColor),
				BorderColor = ReadColor(canvas.Theme, elementNode, "borderColor", new Color(0.0f, 0.0f, 0.0f, 0.0f)),
				BorderThickness = elementNode.GetMemberFloat("borderThickness", 1.0f),
			};
		}

		private static Text BuildText(Canvas canvas, JsonNode elementNode)
		{
			Text text = new Text
			{
				TranslationKey = elementNode.GetMemberString("key", string.Empty),
				Literal = elementNode.GetMemberString("text", string.Empty),
				PixelSize = elementNode.GetMemberFloat("pixelSize", 0.0f),
				Color = ReadColor(canvas.Theme, elementNode, "color", canvas.Theme.TextColor),
				Alignment = ReadAlignment(elementNode.GetMemberString("alignment", "Left")),
				VerticalAlignment = ReadVerticalAlignment(elementNode.GetMemberString("verticalAlignment", "Top")),
			};
			return text;
		}

		private static Button BuildButton(Canvas canvas, JsonNode elementNode)
		{
			Button button = new Button(elementNode.GetMemberString("key", string.Empty))
			{
				Literal = elementNode.GetMemberString("text", string.Empty),
				ShowBorder = elementNode.GetMemberBool("showBorder", true),
				ShowPressAccent = elementNode.GetMemberBool("showPressAccent", true),
				Label =
				{
					Color = ReadColor(canvas.Theme, elementNode, "labelColor", canvas.Theme.TextColor),
					PixelSize = elementNode.GetMemberFloat("labelPixelSize", 0.0f),
				},
			};

			JsonNode background = elementNode.GetMember("background");
			if (background.Type == JsonValueType.String)
			{
				button.BackgroundOverride = ReadColor(canvas.Theme, elementNode, "background", canvas.Theme.SurfaceColor);
			}

			return button;
		}

		/// <summary>
		/// "rect": [x, y, width, height] in pixels, relative to the element's
		/// parent. A layout that names three or four numbers gets them; anything
		/// else falls back to an empty rectangle at the origin.
		/// </summary>
		private static UiRect ReadRect(JsonNode elementNode, string elementName)
		{
			float[] values = new float[4];
			if (!elementNode.TryGetMemberNumberArray("rect", values))
			{
				if (elementNode.GetMember("rect").IsValid)
				{
					Debug.LogError("UiLayoutLoader: the element '" + elementName + "' has a 'rect' that is not an array of numbers.");
				}
				return new UiRect(0.0f, 0.0f, 0.0f, 0.0f);
			}

			return new UiRect(values[0], values[1], values[2], values[3]);
		}

		/// <summary>
		/// A colour is a theme ROLE by name ("PanelColor") or a literal
		/// "#RRGGBB" / "#RRGGBBAA"; anything else keeps the fallback and is
		/// reported, because a silent black rectangle is worse than a log line.
		/// </summary>
		private static Color ReadColor(UiTheme theme, JsonNode elementNode, string memberName, Color fallback)
		{
			JsonNode colorNode = elementNode.GetMember(memberName);
			if (!colorNode.IsValid)
			{
				return fallback;
			}

			string colorText = colorNode.GetString(string.Empty);
			if (colorText.Length == 0)
			{
				return fallback;
			}

			if (TryResolveThemeColor(theme, colorText, out Color themeColor))
			{
				return themeColor;
			}

			if (TryParseHexColor(colorText, out Color literalColor))
			{
				return literalColor;
			}

			Debug.LogError("UiLayoutLoader: '" + colorText + "' is neither a theme colour nor a #RRGGBB(A) literal; '"
				+ memberName + "' keeps its default.");
			return fallback;
		}

		/// <summary>The colours a layout may name, by the theme field they read.</summary>
		public static bool TryResolveThemeColor(UiTheme theme, string colorName, out Color color)
		{
			switch (colorName)
			{
				case "CanvasColor": color = theme.CanvasColor; return true;
				case "PanelColor": color = theme.PanelColor; return true;
				case "SurfaceColor": color = theme.SurfaceColor; return true;
				case "SurfaceHoverColor": color = theme.SurfaceHoverColor; return true;
				case "SurfacePressedColor": color = theme.SurfacePressedColor; return true;
				case "SurfaceDisabledColor": color = theme.SurfaceDisabledColor; return true;
				case "AccentColor": color = theme.AccentColor; return true;
				case "AccentMutedColor": color = theme.AccentMutedColor; return true;
				case "BorderColor": color = theme.BorderColor; return true;
				case "DividerColor": color = theme.DividerColor; return true;
				case "TextColor": color = theme.TextColor; return true;
				case "TextMutedColor": color = theme.TextMutedColor; return true;
				case "TextOnAccentColor": color = theme.TextOnAccentColor; return true;
				case "TextDisabledColor": color = theme.TextDisabledColor; return true;
				default:
					color = default;
					return false;
			}
		}

		/// <summary>"#RGB", "#RRGGBB" and "#RRGGBBAA" (with or without the '#').</summary>
		public static bool TryParseHexColor(string colorText, out Color color)
		{
			color = default;

			string digits = colorText.StartsWith("#", StringComparison.Ordinal) ? colorText.Substring(1) : colorText;
			if (digits.Length != 3 && digits.Length != 6 && digits.Length != 8)
			{
				return false;
			}

			if (digits.Length == 3)
			{
				// #RGB: every digit is doubled, so "#F80" is "#FF8800".
				digits = string.Concat(digits[0], digits[0], digits[1], digits[1], digits[2], digits[2]);
			}

			if (!uint.TryParse(digits, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint packed))
			{
				return false;
			}

			if (digits.Length == 6)
			{
				packed = (packed << 8) | 0xFFu;
			}

			color = new Color(
				((packed >> 24) & 0xFFu) / 255.0f,
				((packed >> 16) & 0xFFu) / 255.0f,
				((packed >> 8) & 0xFFu) / 255.0f,
				(packed & 0xFFu) / 255.0f);
			return true;
		}

		/// <summary>
		/// Optional per-layout theme overrides: any colour a widget draws with can
		/// be replaced by name, so a layout carries its own look without a theme
		/// class of its own.
		/// </summary>
		private static void ApplyThemeOverrides(UiTheme theme, JsonNode themeNode)
		{
			if (!themeNode.IsObject)
			{
				return;
			}

			for (int memberIndex = 0; memberIndex < themeNode.MemberCount; ++memberIndex)
			{
				string memberName = themeNode.GetMemberName(memberIndex);
				string memberText = themeNode.GetMember(memberIndex).GetString(string.Empty);
				if (memberText.Length == 0)
				{
					continue;
				}

				// A theme override is always a literal colour: naming another theme
				// colour here would be a rename, not an override.
				if (!TryParseHexColor(memberText, out Color color))
				{
					Debug.LogError("UiLayoutLoader: the theme override '" + memberName + "' is not a #RRGGBB(A) colour.");
					continue;
				}

				if (!TryApplyThemeColor(theme, memberName, color))
				{
					Debug.LogError("UiLayoutLoader: '" + memberName + "' is not a theme colour; the override is ignored.");
				}
			}
		}

		private static bool TryApplyThemeColor(UiTheme theme, string colorName, Color color)
		{
			switch (colorName)
			{
				case "CanvasColor": theme.CanvasColor = color; return true;
				case "PanelColor": theme.PanelColor = color; return true;
				case "SurfaceColor": theme.SurfaceColor = color; return true;
				case "SurfaceHoverColor": theme.SurfaceHoverColor = color; return true;
				case "SurfacePressedColor": theme.SurfacePressedColor = color; return true;
				case "SurfaceDisabledColor": theme.SurfaceDisabledColor = color; return true;
				case "AccentColor": theme.AccentColor = color; return true;
				case "AccentMutedColor": theme.AccentMutedColor = color; return true;
				case "BorderColor": theme.BorderColor = color; return true;
				case "DividerColor": theme.DividerColor = color; return true;
				case "TextColor": theme.TextColor = color; return true;
				case "TextMutedColor": theme.TextMutedColor = color; return true;
				case "TextOnAccentColor": theme.TextOnAccentColor = color; return true;
				case "TextDisabledColor": theme.TextDisabledColor = color; return true;
				default: return false;
			}
		}

		private static UiTextAlignment ReadAlignment(string alignmentText) => alignmentText.ToLowerInvariant() switch
		{
			"center" => UiTextAlignment.Center,
			"right" => UiTextAlignment.Right,
			_ => UiTextAlignment.Left,
		};

		private static UiTextVerticalAlignment ReadVerticalAlignment(string alignmentText) => alignmentText.ToLowerInvariant() switch
		{
			"middle" => UiTextVerticalAlignment.Middle,
			"bottom" => UiTextVerticalAlignment.Bottom,
			_ => UiTextVerticalAlignment.Top,
		};
	}
}

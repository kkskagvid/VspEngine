using System;

namespace VspEngine.Rendering
{
	/// <summary>
	/// Straight (non-premultiplied) RGBA color in the 0..1 range, matching what
	/// the wrapped graphics API expects for clear values and shader constants.
	/// </summary>
	public readonly struct Color : IEquatable<Color>
	{
		public readonly float R;
		public readonly float G;
		public readonly float B;
		public readonly float A;

		public Color(float red, float green, float blue, float alpha)
		{
			R = red;
			G = green;
			B = blue;
			A = alpha;
		}

		public Color(float red, float green, float blue) : this(red, green, blue, 1.0f) { }

		public static Color Clear => new Color(0.0f, 0.0f, 0.0f, 0.0f);
		public static Color Black => new Color(0.0f, 0.0f, 0.0f, 1.0f);
		public static Color White => new Color(1.0f, 1.0f, 1.0f, 1.0f);
		public static Color Red => new Color(1.0f, 0.0f, 0.0f, 1.0f);
		public static Color Green => new Color(0.0f, 1.0f, 0.0f, 1.0f);
		public static Color Blue => new Color(0.0f, 0.0f, 1.0f, 1.0f);

		public bool Equals(Color other) => R == other.R && G == other.G && B == other.B && A == other.A;

		public override bool Equals(object? obj) => obj is Color other && Equals(other);

		public override int GetHashCode() => HashCode.Combine(R, G, B, A);

		public override string ToString() => "(" + R + ", " + G + ", " + B + ", " + A + ")";
	}
}

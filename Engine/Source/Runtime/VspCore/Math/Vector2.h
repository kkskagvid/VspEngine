#pragma once

#include <cmath>

#include "Core/Core.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Vector2
	// -------------------------------------------------------------------------
	// Two-component float vector (x, y) with the arithmetic the engine needs.
	//
	// The engine's own vector maths - used by the UI layout, the camera rigs and
	// every native helper - so the native layer never has to include a
	// third-party maths library for the common operations.
	//
	// Like the rest of the runtime, nothing here throws and nothing allocates.
	// -------------------------------------------------------------------------
	struct Vector2
	{
		float fX = 0.0f;
		float fY = 0.0f;

		// -------- Ready-made values --------
		// Declared here and defined after the class, because a static data member
		// of an incomplete type cannot be initialised inside its own definition.
		static const Vector2 Zero;
		static const Vector2 One;
		static const Vector2 UnitX;
		static const Vector2 UnitY;

		// -------- Construction --------
		constexpr Vector2() = default;
		constexpr Vector2(float fInX, float fInY)
			: fX(fInX)
			, fY(fInY)
		{
		}

		explicit constexpr Vector2(float fScalar)
			: fX(fScalar)
			, fY(fScalar)
		{
		}

		// -------- Component access --------
		constexpr float operator[](size_t nComponentIndex) const
		{
			return (nComponentIndex == 0) ? fX : fY;
		}

		constexpr float& operator[](size_t nComponentIndex)
		{
			return (nComponentIndex == 0) ? fX : fY;
		}

		// -------- Arithmetic --------
		constexpr Vector2 operator+(const Vector2& Other) const { return Vector2(fX + Other.fX, fY + Other.fY); }
		constexpr Vector2 operator-(const Vector2& Other) const { return Vector2(fX - Other.fX, fY - Other.fY); }
		constexpr Vector2 operator*(float fScalar) const { return Vector2(fX * fScalar, fY * fScalar); }
		constexpr Vector2 operator/(float fScalar) const { return Vector2(fX / fScalar, fY / fScalar); }
		constexpr Vector2 operator-() const { return Vector2(-fX, -fY); }

		constexpr Vector2& operator+=(const Vector2& Other) { fX += Other.fX; fY += Other.fY; return *this; }
		constexpr Vector2& operator-=(const Vector2& Other) { fX -= Other.fX; fY -= Other.fY; return *this; }
		constexpr Vector2& operator*=(float fScalar) { fX *= fScalar; fY *= fScalar; return *this; }
		constexpr Vector2& operator/=(float fScalar) { fX /= fScalar; fY /= fScalar; return *this; }

		constexpr bool operator==(const Vector2& Other) const { return fX == Other.fX && fY == Other.fY; }
		constexpr bool operator!=(const Vector2& Other) const { return !(*this == Other); }

		// -------- Measurement --------
		float GetLengthSquared() const { return (fX * fX) + (fY * fY); }
		float GetLength() const { return std::sqrt(GetLengthSquared()); }

		// Unit vector in the same direction; a zero-length vector stays zero
		// (the caller gets Zero back instead of a division by zero).
		Vector2 GetNormalized() const
		{
			const float fLengthSquared = GetLengthSquared();
			if (fLengthSquared <= 0.0f)
			{
				return Zero;
			}
			return *this / std::sqrt(fLengthSquared);
		}

		// Normalizes in place and reports the length it had.
		float Normalize()
		{
			const float fLength = GetLength();
			if (fLength > 0.0f)
			{
				fX /= fLength;
				fY /= fLength;
			}
			return fLength;
		}

		float Dot(const Vector2& Other) const { return (fX * Other.fX) + (fY * Other.fY); }

		// Z component of the 3D cross product: positive when Other is
		// counter-clockwise from this vector.
		float Cross(const Vector2& Other) const { return (fX * Other.fY) - (fY * Other.fX); }

		float DistanceTo(const Vector2& Other) const { return (*this - Other).GetLength(); }
		float DistanceSquaredTo(const Vector2& Other) const { return (*this - Other).GetLengthSquared(); }

		// Counter-clockwise perpendicular.
		constexpr Vector2 GetPerpendicular() const { return Vector2(-fY, fX); }

		// -------- Helpers --------
		static constexpr Vector2 Lerp(const Vector2& From, const Vector2& To, float fFactor)
		{
			return Vector2(
				From.fX + ((To.fX - From.fX) * fFactor),
				From.fY + ((To.fY - From.fY) * fFactor));
		}

		static constexpr Vector2 Min(const Vector2& Left, const Vector2& Right)
		{
			return Vector2(Left.fX < Right.fX ? Left.fX : Right.fX, Left.fY < Right.fY ? Left.fY : Right.fY);
		}

		static constexpr Vector2 Max(const Vector2& Left, const Vector2& Right)
		{
			return Vector2(Left.fX > Right.fX ? Left.fX : Right.fX, Left.fY > Right.fY ? Left.fY : Right.fY);
		}

		// Component-wise product.
		static constexpr Vector2 Scale(const Vector2& Left, const Vector2& Right)
		{
			return Vector2(Left.fX * Right.fX, Left.fY * Right.fY);
		}
	};

	inline constexpr Vector2 operator*(float fScalar, const Vector2& Value)
	{
		return Vector2(Value.fX * fScalar, Value.fY * fScalar);
	}

	inline constexpr Vector2 Vector2::Zero{ 0.0f, 0.0f };
	inline constexpr Vector2 Vector2::One{ 1.0f, 1.0f };
	inline constexpr Vector2 Vector2::UnitX{ 1.0f, 0.0f };
	inline constexpr Vector2 Vector2::UnitY{ 0.0f, 1.0f };
}

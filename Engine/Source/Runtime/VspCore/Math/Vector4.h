#pragma once

#include <cmath>

#include "Core/Core.h"
#include "Math/Vector3.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Vector4
	// -------------------------------------------------------------------------
	// Four-component float vector (x, y, z, w). Used for colours (RGBA), for
	// homogeneous positions a 4x4 matrix multiplies, and for plane equations.
	//
	// Nothing here throws and nothing allocates.
	// -------------------------------------------------------------------------
	struct Vector4
	{
		float fX = 0.0f;
		float fY = 0.0f;
		float fZ = 0.0f;
		float fW = 0.0f;

		// -------- Ready-made values --------
		// Declared here and defined after the class, because a static data member
		// of an incomplete type cannot be initialised inside its own definition.
		static const Vector4 Zero;
		static const Vector4 One;

		// -------- Construction --------
		constexpr Vector4() = default;
		constexpr Vector4(float fInX, float fInY, float fInZ, float fInW)
			: fX(fInX)
			, fY(fInY)
			, fZ(fInZ)
			, fW(fInW)
		{
		}

		// Promotes a position/direction to homogeneous coordinates with the
		// given w (1 for a point, 0 for a direction).
		constexpr Vector4(const Vector3& Value, float fInW)
			: fX(Value.fX)
			, fY(Value.fY)
			, fZ(Value.fZ)
			, fW(fInW)
		{
		}

		explicit constexpr Vector4(float fScalar)
			: fX(fScalar)
			, fY(fScalar)
			, fZ(fScalar)
			, fW(fScalar)
		{
		}

		// -------- Component access --------
		constexpr float operator[](size_t nComponentIndex) const
		{
			return (nComponentIndex == 0) ? fX : ((nComponentIndex == 1) ? fY : ((nComponentIndex == 2) ? fZ : fW));
		}

		constexpr float& operator[](size_t nComponentIndex)
		{
			return (nComponentIndex == 0) ? fX : ((nComponentIndex == 1) ? fY : ((nComponentIndex == 2) ? fZ : fW));
		}

		// -------- Accessors for the common uses --------
		constexpr Vector3 GetXyz() const { return Vector3(fX, fY, fZ); }

		// -------- Arithmetic --------
		constexpr Vector4 operator+(const Vector4& Other) const { return Vector4(fX + Other.fX, fY + Other.fY, fZ + Other.fZ, fW + Other.fW); }
		constexpr Vector4 operator-(const Vector4& Other) const { return Vector4(fX - Other.fX, fY - Other.fY, fZ - Other.fZ, fW - Other.fW); }
		constexpr Vector4 operator*(float fScalar) const { return Vector4(fX * fScalar, fY * fScalar, fZ * fScalar, fW * fScalar); }
		constexpr Vector4 operator/(float fScalar) const { return Vector4(fX / fScalar, fY / fScalar, fZ / fScalar, fW / fScalar); }
		constexpr Vector4 operator-() const { return Vector4(-fX, -fY, -fZ, -fW); }

		constexpr Vector4& operator+=(const Vector4& Other) { fX += Other.fX; fY += Other.fY; fZ += Other.fZ; fW += Other.fW; return *this; }
		constexpr Vector4& operator-=(const Vector4& Other) { fX -= Other.fX; fY -= Other.fY; fZ -= Other.fZ; fW -= Other.fW; return *this; }
		constexpr Vector4& operator*=(float fScalar) { fX *= fScalar; fY *= fScalar; fZ *= fScalar; fW *= fScalar; return *this; }

		constexpr bool operator==(const Vector4& Other) const { return fX == Other.fX && fY == Other.fY && fZ == Other.fZ && fW == Other.fW; }
		constexpr bool operator!=(const Vector4& Other) const { return !(*this == Other); }

		// -------- Measurement --------
		float GetLengthSquared() const { return (fX * fX) + (fY * fY) + (fZ * fZ) + (fW * fW); }
		float GetLength() const { return std::sqrt(GetLengthSquared()); }

		Vector4 GetNormalized() const
		{
			const float fLengthSquared = GetLengthSquared();
			if (fLengthSquared <= 0.0f)
			{
				return Zero;
			}
			return *this / std::sqrt(fLengthSquared);
		}

		float Dot(const Vector4& Other) const { return (fX * Other.fX) + (fY * Other.fY) + (fZ * Other.fZ) + (fW * Other.fW); }

		// -------- Helpers --------
		static constexpr Vector4 Lerp(const Vector4& From, const Vector4& To, float fFactor)
		{
			return Vector4(
				From.fX + ((To.fX - From.fX) * fFactor),
				From.fY + ((To.fY - From.fY) * fFactor),
				From.fZ + ((To.fZ - From.fZ) * fFactor),
				From.fW + ((To.fW - From.fW) * fFactor));
		}

		// Component-wise product, which is how a colour is tinted by another.
		static constexpr Vector4 Scale(const Vector4& Left, const Vector4& Right)
		{
			return Vector4(Left.fX * Right.fX, Left.fY * Right.fY, Left.fZ * Right.fZ, Left.fW * Right.fW);
		}
	};

	inline constexpr Vector4 operator*(float fScalar, const Vector4& Value)
	{
		return Vector4(Value.fX * fScalar, Value.fY * fScalar, Value.fZ * fScalar, Value.fW * fScalar);
	}

	inline constexpr Vector4 Vector4::Zero{ 0.0f, 0.0f, 0.0f, 0.0f };
	inline constexpr Vector4 Vector4::One{ 1.0f, 1.0f, 1.0f, 1.0f };
}

#pragma once

#include <cmath>

#include "Core/Core.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Vector3
	// -------------------------------------------------------------------------
	// Three-component float vector (x, y, z), the engine's point/direction type.
	//
	// Right-handed, Y up: an object's forward is its local -Z, its up is +Y and
	// its right is +X, which is the convention Transform and Camera use.
	//
	// Nothing here throws and nothing allocates.
	// -------------------------------------------------------------------------
	struct Vector3
	{
		float fX = 0.0f;
		float fY = 0.0f;
		float fZ = 0.0f;

		// -------- Ready-made values --------
		// Declared here and defined after the class, because a static data member
		// of an incomplete type cannot be initialised inside its own definition.
		static const Vector3 Zero;
		static const Vector3 One;
		static const Vector3 UnitX;
		static const Vector3 UnitY;
		static const Vector3 UnitZ;
		static const Vector3 Forward;
		static const Vector3 Up;
		static const Vector3 Right;

		// -------- Construction --------
		constexpr Vector3() = default;
		constexpr Vector3(float fInX, float fInY, float fInZ)
			: fX(fInX)
			, fY(fInY)
			, fZ(fInZ)
		{
		}

		explicit constexpr Vector3(float fScalar)
			: fX(fScalar)
			, fY(fScalar)
			, fZ(fScalar)
		{
		}

		// -------- Component access --------
		constexpr float operator[](size_t nComponentIndex) const
		{
			return (nComponentIndex == 0) ? fX : ((nComponentIndex == 1) ? fY : fZ);
		}

		constexpr float& operator[](size_t nComponentIndex)
		{
			return (nComponentIndex == 0) ? fX : ((nComponentIndex == 1) ? fY : fZ);
		}

		// -------- Arithmetic --------
		constexpr Vector3 operator+(const Vector3& Other) const { return Vector3(fX + Other.fX, fY + Other.fY, fZ + Other.fZ); }
		constexpr Vector3 operator-(const Vector3& Other) const { return Vector3(fX - Other.fX, fY - Other.fY, fZ - Other.fZ); }
		constexpr Vector3 operator*(float fScalar) const { return Vector3(fX * fScalar, fY * fScalar, fZ * fScalar); }
		constexpr Vector3 operator/(float fScalar) const { return Vector3(fX / fScalar, fY / fScalar, fZ / fScalar); }
		constexpr Vector3 operator-() const { return Vector3(-fX, -fY, -fZ); }

		constexpr Vector3& operator+=(const Vector3& Other) { fX += Other.fX; fY += Other.fY; fZ += Other.fZ; return *this; }
		constexpr Vector3& operator-=(const Vector3& Other) { fX -= Other.fX; fY -= Other.fY; fZ -= Other.fZ; return *this; }
		constexpr Vector3& operator*=(float fScalar) { fX *= fScalar; fY *= fScalar; fZ *= fScalar; return *this; }
		constexpr Vector3& operator/=(float fScalar) { fX /= fScalar; fY /= fScalar; fZ /= fScalar; return *this; }

		constexpr bool operator==(const Vector3& Other) const { return fX == Other.fX && fY == Other.fY && fZ == Other.fZ; }
		constexpr bool operator!=(const Vector3& Other) const { return !(*this == Other); }

		// -------- Measurement --------
		float GetLengthSquared() const { return (fX * fX) + (fY * fY) + (fZ * fZ); }
		float GetLength() const { return std::sqrt(GetLengthSquared()); }

		// Unit vector in the same direction; a zero-length vector stays zero.
		Vector3 GetNormalized() const
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
				fZ /= fLength;
			}
			return fLength;
		}

		float Dot(const Vector3& Other) const { return (fX * Other.fX) + (fY * Other.fY) + (fZ * Other.fZ); }
		float DistanceTo(const Vector3& Other) const { return (*this - Other).GetLength(); }
		float DistanceSquaredTo(const Vector3& Other) const { return (*this - Other).GetLengthSquared(); }

		// -------- Helpers --------
		static constexpr Vector3 Cross(const Vector3& Left, const Vector3& Right)
		{
			return Vector3(
				(Left.fY * Right.fZ) - (Left.fZ * Right.fY),
				(Left.fZ * Right.fX) - (Left.fX * Right.fZ),
				(Left.fX * Right.fY) - (Left.fY * Right.fX));
		}

		static constexpr float Dot(const Vector3& Left, const Vector3& Right)
		{
			return (Left.fX * Right.fX) + (Left.fY * Right.fY) + (Left.fZ * Right.fZ);
		}

		static constexpr Vector3 Lerp(const Vector3& From, const Vector3& To, float fFactor)
		{
			return Vector3(
				From.fX + ((To.fX - From.fX) * fFactor),
				From.fY + ((To.fY - From.fY) * fFactor),
				From.fZ + ((To.fZ - From.fZ) * fFactor));
		}

		static constexpr Vector3 Min(const Vector3& Left, const Vector3& Right)
		{
			return Vector3(
				Left.fX < Right.fX ? Left.fX : Right.fX,
				Left.fY < Right.fY ? Left.fY : Right.fY,
				Left.fZ < Right.fZ ? Left.fZ : Right.fZ);
		}

		static constexpr Vector3 Max(const Vector3& Left, const Vector3& Right)
		{
			return Vector3(
				Left.fX > Right.fX ? Left.fX : Right.fX,
				Left.fY > Right.fY ? Left.fY : Right.fY,
				Left.fZ > Right.fZ ? Left.fZ : Right.fZ);
		}

		// Component-wise product.
		static constexpr Vector3 Scale(const Vector3& Left, const Vector3& Right)
		{
			return Vector3(Left.fX * Right.fX, Left.fY * Right.fY, Left.fZ * Right.fZ);
		}

		// The vector a spherical direction points along: yaw turns about +Y,
		// pitch rises above the horizon. Both angles are in radians, and yaw 0
		// looks down -Z (the engine's forward).
		static Vector3 FromSpherical(float fYawRadians, float fPitchRadians)
		{
			const float fCosPitch = std::cos(fPitchRadians);
			return Vector3(
				std::sin(fYawRadians) * fCosPitch,
				std::sin(fPitchRadians),
				-std::cos(fYawRadians) * fCosPitch);
		}
	};

	inline constexpr Vector3 operator*(float fScalar, const Vector3& Value)
	{
		return Vector3(Value.fX * fScalar, Value.fY * fScalar, Value.fZ * fScalar);
	}

	inline constexpr Vector3 Vector3::Zero{ 0.0f, 0.0f, 0.0f };
	inline constexpr Vector3 Vector3::One{ 1.0f, 1.0f, 1.0f };
	inline constexpr Vector3 Vector3::UnitX{ 1.0f, 0.0f, 0.0f };
	inline constexpr Vector3 Vector3::UnitY{ 0.0f, 1.0f, 0.0f };
	inline constexpr Vector3 Vector3::UnitZ{ 0.0f, 0.0f, 1.0f };
	inline constexpr Vector3 Vector3::Forward{ 0.0f, 0.0f, -1.0f };
	inline constexpr Vector3 Vector3::Up{ 0.0f, 1.0f, 0.0f };
	inline constexpr Vector3 Vector3::Right{ 1.0f, 0.0f, 0.0f };
}

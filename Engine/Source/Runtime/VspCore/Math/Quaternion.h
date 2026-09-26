#pragma once

#include <cmath>

#include "Core/Core.h"
#include "Math/Matrix4x4.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Quaternion
	// -------------------------------------------------------------------------
	// Unit quaternion (x, y, z, w) describing a rotation. w is the scalar part,
	// (x, y, z) the vector part.
	//
	// The Euler convention is the engine's: degrees, applied Z then Y then X,
	// i.e. the rotation Rx * Ry * Rz that Transform stores and that
	// Matrix4x4::RotationEulerDegrees builds. FromEulerDegrees and
	// ToEulerDegrees therefore round-trip against Transform's own values.
	//
	// Nothing here throws. An operation that cannot be carried out (normalizing
	// a zero quaternion, inverting one) reports the empty value - the identity,
	// which rotates by nothing - after logging why.
	// -------------------------------------------------------------------------
	struct Quaternion
	{
		float fX = 0.0f;
		float fY = 0.0f;
		float fZ = 0.0f;
		float fW = 1.0f;

		// -------- Ready-made values --------
		// Declared here and defined after the class, because a static data member
		// of an incomplete type cannot be initialised inside its own definition.
		static const Quaternion Identity;

		// -------- Construction --------
		constexpr Quaternion() = default;
		constexpr Quaternion(float fInX, float fInY, float fInZ, float fInW)
			: fX(fInX)
			, fY(fInY)
			, fZ(fInZ)
			, fW(fInW)
		{
		}

		// -------- Conversion --------
		// The engine's Euler angles (degrees, Z then Y then X).
		static Quaternion FromEulerDegrees(float fDegreesX, float fDegreesY, float fDegreesZ);
		static Quaternion FromEulerDegrees(const Vector3& EulerDegrees);

		// A turn of fRadians about Axis, which is normalized first.
		static Quaternion FromAxisAngle(const Vector3& Axis, float fRadians);

		// A rotation that takes FromDirection onto ToDirection.
		static Quaternion FromToRotation(const Vector3& FromDirection, const Vector3& ToDirection);

		// The inverse of FromEulerDegrees: the same angles Transform would read
		// back out of the rotation matrix.
		Vector3 ToEulerDegrees() const;

		Matrix4x4 ToMatrix4x4() const;

		// -------- Measurement --------
		float GetLengthSquared() const { return (fX * fX) + (fY * fY) + (fZ * fZ) + (fW * fW); }
		float GetLength() const { return std::sqrt(GetLengthSquared()); }

		// Unit quaternion; a zero quaternion reports the empty value - the
		// identity - after logging why.
		Quaternion GetNormalized() const;

		float Dot(const Quaternion& Other) const { return (fX * Other.fX) + (fY * Other.fY) + (fZ * Other.fZ) + (fW * Other.fW); }

		// Angle between the two rotations, in radians.
		float GetAngleTo(const Quaternion& Other) const;

		// -------- Operations --------
		// The rotation Other applied after this one.
		Quaternion Multiply(const Quaternion& Other) const;

		// The rotation that undoes this one. For a unit quaternion this is its
		// conjugate.
		Quaternion GetConjugate() const { return Quaternion(-fX, -fY, -fZ, fW); }

		Quaternion GetInverse() const;

		// Takes a direction (or a point treated as a vector about the origin)
		// through the rotation.
		Vector3 RotateVector(const Vector3& Value) const;

		// -------- Helpers --------
		static Quaternion Lerp(const Quaternion& From, const Quaternion& To, float fFactor);

		// Spherical interpolation: the shortest turn between the two rotations,
		// which is what a camera or a turntable wants. Falls back to Lerp when
		// the rotations are almost the same (the sine in the denominator would
		// otherwise vanish).
		static Quaternion Slerp(const Quaternion& From, const Quaternion& To, float fFactor);

		static constexpr Quaternion Scale(const Quaternion& Value, float fScalar)
		{
			return Quaternion(Value.fX * fScalar, Value.fY * fScalar, Value.fZ * fScalar, Value.fW * fScalar);
		}

		constexpr bool operator==(const Quaternion& Other) const
		{
			return fX == Other.fX && fY == Other.fY && fZ == Other.fZ && fW == Other.fW;
		}

		constexpr bool operator!=(const Quaternion& Other) const { return !(*this == Other); }
	};

	inline Quaternion operator*(const Quaternion& Left, const Quaternion& Right)
	{
		return Left.Multiply(Right);
	}

	inline constexpr Quaternion Quaternion::Identity{ 0.0f, 0.0f, 0.0f, 1.0f };
}

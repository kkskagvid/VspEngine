#include "RuntimePCH.h"

#include "Core/Diagnostics/ErrorHandling.h"
#include "Math/Quaternion.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "Math";
	static constexpr float k_fDegreesToRadians = 3.14159265358979f / 180.0f;
	static constexpr float k_fRadiansToDegrees = 180.0f / 3.14159265358979f;

	// -------------------------------------------------------------------------
	// Conversion
	// -------------------------------------------------------------------------

	Quaternion Quaternion::FromEulerDegrees(float fDegreesX, float fDegreesY, float fDegreesZ)
	{
		// Half angles, because a quaternion turns by half the angle it encodes.
		const float fHalfX = fDegreesX * k_fDegreesToRadians * 0.5f;
		const float fHalfY = fDegreesY * k_fDegreesToRadians * 0.5f;
		const float fHalfZ = fDegreesZ * k_fDegreesToRadians * 0.5f;

		const float fSinX = std::sin(fHalfX);
		const float fCosX = std::cos(fHalfX);
		const float fSinY = std::sin(fHalfY);
		const float fCosY = std::cos(fHalfY);
		const float fSinZ = std::sin(fHalfZ);
		const float fCosZ = std::cos(fHalfZ);

		// The Hamilton product qX * qY * qZ, which is exactly the rotation
		// order (Z, then Y, then X) the engine uses.
		return Quaternion(
			(fSinX * fCosY * fCosZ) + (fCosX * fSinY * fSinZ),
			(fCosX * fSinY * fCosZ) - (fSinX * fCosY * fSinZ),
			(fCosX * fCosY * fSinZ) + (fSinX * fSinY * fCosZ),
			(fCosX * fCosY * fCosZ) - (fSinX * fSinY * fSinZ));
	}

	Quaternion Quaternion::FromEulerDegrees(const Vector3& EulerDegrees)
	{
		return FromEulerDegrees(EulerDegrees.fX, EulerDegrees.fY, EulerDegrees.fZ);
	}

	Quaternion Quaternion::FromAxisAngle(const Vector3& Axis, float fRadians)
	{
		const float fAxisLengthSquared = Axis.GetLengthSquared();
		if (fAxisLengthSquared <= 0.0f)
		{
			VSP_LOG_ERROR(kLogTag, "Quaternion::FromAxisAngle got a zero-length axis; the identity is returned.");
			return Identity;
		}

		const float fInverseLength = 1.0f / std::sqrt(fAxisLengthSquared);
		const float fHalfAngle = fRadians * 0.5f;
		const float fSine = std::sin(fHalfAngle);

		return Quaternion(
			Axis.fX * fInverseLength * fSine,
			Axis.fY * fInverseLength * fSine,
			Axis.fZ * fInverseLength * fSine,
			std::cos(fHalfAngle));
	}

	Quaternion Quaternion::FromToRotation(const Vector3& FromDirection, const Vector3& ToDirection)
	{
		const Vector3 from = FromDirection.GetNormalized();
		const Vector3 to = ToDirection.GetNormalized();
		if (from.GetLengthSquared() <= 0.0f || to.GetLengthSquared() <= 0.0f)
		{
			VSP_LOG_ERROR(kLogTag, "Quaternion::FromToRotation got a zero-length direction; the identity is returned.");
			return Identity;
		}

		const float fDot = Vector3::Dot(from, to);
		if (fDot >= 1.0f - 1e-6f)
		{
			return Identity;   // Already pointing the same way.
		}
		if (fDot <= -1.0f + 1e-6f)
		{
			// Opposite directions: any axis perpendicular to "from" works, so
			// one is picked deterministically.
			const Vector3 fallbackAxis = (std::fabs(from.fX) < 0.9f) ? Vector3(1.0f, 0.0f, 0.0f) : Vector3(0.0f, 1.0f, 0.0f);
			return FromAxisAngle(Vector3::Cross(from, fallbackAxis), 3.14159265358979f);
		}

		const Vector3 cross = Vector3::Cross(from, to);
		return Quaternion(cross.fX, cross.fY, cross.fZ, 1.0f + fDot).GetNormalized();
	}

	Vector3 Quaternion::ToEulerDegrees() const
	{
		// Read the angles back out of the rotation matrix with the same
		// formulas Transform::GetWorldRotation uses, so both agree bit for bit.
		const Matrix4x4 rotation = ToMatrix4x4();

		const float fM00 = rotation.GetElement(0, 0);
		const float fM01 = rotation.GetElement(0, 1);
		const float fM02 = rotation.GetElement(0, 2);
		const float fM10 = rotation.GetElement(1, 0);
		const float fM11 = rotation.GetElement(1, 1);
		const float fM12 = rotation.GetElement(1, 2);
		const float fM22 = rotation.GetElement(2, 2);

		float fClampedM02 = fM02;
		if (fClampedM02 < -1.0f)
		{
			fClampedM02 = -1.0f;
		}
		else if (fClampedM02 > 1.0f)
		{
			fClampedM02 = 1.0f;
		}

		float fRadiansX = 0.0f;
		float fRadiansZ = 0.0f;
		const float fRadiansY = std::asin(fClampedM02);
		if (std::fabs(fM02) < 0.9999f)
		{
			fRadiansX = std::atan2(-fM12, fM22);
			fRadiansZ = std::atan2(-fM01, fM00);
		}
		else
		{
			// Looking straight up or down: X and Z turn about the same axis, so
			// the whole turn is reported as X.
			fRadiansX = std::atan2((fM02 > 0.0f) ? fM10 : -fM10, fM11);
		}

		return Vector3(fRadiansX * k_fRadiansToDegrees, fRadiansY * k_fRadiansToDegrees, fRadiansZ * k_fRadiansToDegrees);
	}

	Matrix4x4 Quaternion::ToMatrix4x4() const
	{
		const float fXx = fX * fX;
		const float fYy = fY * fY;
		const float fZz = fZ * fZ;
		const float fXy = fX * fY;
		const float fXz = fX * fZ;
		const float fYz = fY * fZ;
		const float fWx = fW * fX;
		const float fWy = fW * fY;
		const float fWz = fW * fZ;

		Matrix4x4 result = Matrix4x4::Identity();
		result.SetElement(0, 0, 1.0f - (2.0f * (fYy + fZz)));
		result.SetElement(0, 1, 2.0f * (fXy - fWz));
		result.SetElement(0, 2, 2.0f * (fXz + fWy));

		result.SetElement(1, 0, 2.0f * (fXy + fWz));
		result.SetElement(1, 1, 1.0f - (2.0f * (fXx + fZz)));
		result.SetElement(1, 2, 2.0f * (fYz - fWx));

		result.SetElement(2, 0, 2.0f * (fXz - fWy));
		result.SetElement(2, 1, 2.0f * (fYz + fWx));
		result.SetElement(2, 2, 1.0f - (2.0f * (fXx + fYy)));
		return result;
	}

	// -------------------------------------------------------------------------
	// Measurement
	// -------------------------------------------------------------------------

	Quaternion Quaternion::GetNormalized() const
	{
		const float fLengthSquared = GetLengthSquared();
		if (fLengthSquared <= 0.0f)
		{
			VSP_LOG_ERROR(kLogTag, "Quaternion::GetNormalized got a zero quaternion; the identity is returned.");
			return Identity;
		}

		const float fInverseLength = 1.0f / std::sqrt(fLengthSquared);
		return Quaternion(fX * fInverseLength, fY * fInverseLength, fZ * fInverseLength, fW * fInverseLength);
	}

	float Quaternion::GetAngleTo(const Quaternion& Other) const
	{
		// The angle between two rotations is twice the angle between the
		// quaternions that represent them. The absolute dot keeps the turn on
		// the short side.
		float fDot = Dot(Other);
		if (fDot < 0.0f)
		{
			fDot = -fDot;
		}
		if (fDot > 1.0f)
		{
			fDot = 1.0f;
		}
		return 2.0f * std::acos(fDot);
	}

	// -------------------------------------------------------------------------
	// Operations
	// -------------------------------------------------------------------------

	Quaternion Quaternion::Multiply(const Quaternion& Other) const
	{
		return Quaternion(
			(fW * Other.fX) + (fX * Other.fW) + (fY * Other.fZ) - (fZ * Other.fY),
			(fW * Other.fY) - (fX * Other.fZ) + (fY * Other.fW) + (fZ * Other.fX),
			(fW * Other.fZ) + (fX * Other.fY) - (fY * Other.fX) + (fZ * Other.fW),
			(fW * Other.fW) - (fX * Other.fX) - (fY * Other.fY) - (fZ * Other.fZ));
	}

	Quaternion Quaternion::GetInverse() const
	{
		const float fLengthSquared = GetLengthSquared();
		if (fLengthSquared <= 0.0f)
		{
			VSP_LOG_ERROR(kLogTag, "Quaternion::GetInverse got a zero quaternion; the identity is returned.");
			return Identity;
		}

		const float fInverseLengthSquared = 1.0f / fLengthSquared;
		return Quaternion(
			-fX * fInverseLengthSquared,
			-fY * fInverseLengthSquared,
			-fZ * fInverseLengthSquared,
			fW * fInverseLengthSquared);
	}

	Vector3 Quaternion::RotateVector(const Vector3& Value) const
	{
		// v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v) - the compact
		// form of q * (v, 0) * q^-1 that needs no quaternion multiply.
		const Vector3 vectorPart(fX, fY, fZ);
		const Vector3 firstCross = Vector3::Cross(vectorPart, Value);
		const Vector3 afterW = Vector3(
			firstCross.fX + (fW * Value.fX),
			firstCross.fY + (fW * Value.fY),
			firstCross.fZ + (fW * Value.fZ));
		const Vector3 secondCross = Vector3::Cross(vectorPart, afterW);

		return Vector3(
			Value.fX + (2.0f * secondCross.fX),
			Value.fY + (2.0f * secondCross.fY),
			Value.fZ + (2.0f * secondCross.fZ));
	}

	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	Quaternion Quaternion::Lerp(const Quaternion& From, const Quaternion& To, float fFactor)
	{
		// Taking the shorter arc keeps the interpolation from swinging the long
		// way round.
		const float fDot = From.Dot(To);
		const Quaternion target = (fDot < 0.0f) ? Quaternion(-To.fX, -To.fY, -To.fZ, -To.fW) : To;

		return Quaternion(
			From.fX + ((target.fX - From.fX) * fFactor),
			From.fY + ((target.fY - From.fY) * fFactor),
			From.fZ + ((target.fZ - From.fZ) * fFactor),
			From.fW + ((target.fW - From.fW) * fFactor)).GetNormalized();
	}

	Quaternion Quaternion::Slerp(const Quaternion& From, const Quaternion& To, float fFactor)
	{
		float fDot = From.Dot(To);
		Quaternion target = To;
		if (fDot < 0.0f)
		{
			target = Quaternion(-To.fX, -To.fY, -To.fZ, -To.fW);
			fDot = -fDot;
		}

		// Nearly parallel: a plain interpolation is both cheaper and stable.
		if (fDot > 0.9995f)
		{
			return Lerp(From, target, fFactor);
		}

		if (fDot > 1.0f)
		{
			fDot = 1.0f;
		}

		const float fAngle = std::acos(fDot);
		const float fInverseSine = 1.0f / std::sin(fAngle);
		const float fFromWeight = std::sin((1.0f - fFactor) * fAngle) * fInverseSine;
		const float fToWeight = std::sin(fFactor * fAngle) * fInverseSine;

		return Quaternion(
			(From.fX * fFromWeight) + (target.fX * fToWeight),
			(From.fY * fFromWeight) + (target.fY * fToWeight),
			(From.fZ * fFromWeight) + (target.fZ * fToWeight),
			(From.fW * fFromWeight) + (target.fW * fToWeight)).GetNormalized();
	}
}

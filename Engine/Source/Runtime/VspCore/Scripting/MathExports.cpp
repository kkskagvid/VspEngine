#include "RuntimePCH.h"

#include "Core/Diagnostics/ErrorHandling.h"
#include "Math/Matrix4x4.h"
#include "Math/Quaternion.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "Scripting/ScriptExport.h"

// -------------------------------------------------------------------------
// Vector and matrix exports consumed by managed code (C# -> C++ direction).
// VspEngine.NativeMath P/Invokes these exact names from VspCore.dll.
//
// The engine's vector and matrix maths lives HERE, in the native layer, so the
// managed side has one implementation to agree with instead of a second one of
// its own. Every call is plain data in and plain data out: fixed-size float
// arrays that mirror the struct layouts in Math/.
//
// Conventions (see Math/Matrix4x4.h):
//   * vectors are (x, y, z) / (x, y, z, w) float triples/quads,
//   * matrices are COLUMN-MAJOR 16-float arrays, element (row, column) at
//     column * 4 + row,
//   * Euler angles are degrees, applied Z then Y then X.
//
// A function that cannot do its work writes the empty value - a zero matrix,
// a zero vector - and logs the reason; it never reports success.
// -------------------------------------------------------------------------

static constexpr const char* kLogTag = "MathExports";

namespace
{
	// Reads three floats into a vector.
	Vsp::Vector3 ReadVector3(const float* pValues)
	{
		return Vsp::Vector3(pValues[0], pValues[1], pValues[2]);
	}

	// Writes a vector into three floats.
	void WriteVector3(const Vsp::Vector3& Value, float* pOutValues)
	{
		pOutValues[0] = Value.fX;
		pOutValues[1] = Value.fY;
		pOutValues[2] = Value.fZ;
	}

	// Reads a column-major 16-float matrix.
	Vsp::Matrix4x4 ReadMatrix4x4(const float* pValues)
	{
		return Vsp::Matrix4x4(pValues);
	}

	void WriteMatrix4x4(const Vsp::Matrix4x4& Value, float* pOutValues)
	{
		for (size_t nElementIndex = 0; nElementIndex < 16; ++nElementIndex)
		{
			pOutValues[nElementIndex] = Value.fElements[nElementIndex];
		}
	}

	// The buffer every export writes into must be there.
	bool AreBuffersValid(const float* pFirst, const float* pSecond)
	{
		if (pFirst == nullptr || pSecond == nullptr)
		{
			VSP_LOG_ERROR(kLogTag, "An export was given a null buffer; nothing was written.");
			return false;
		}
		return true;
	}
}

// -------- Vector2 --------

CSHARP_EXPORT void VspMath_Vector2Add(const float* pLeft, const float* pRight, float* pOutValues)
{
	if (!AreBuffersValid(pLeft, pRight) || pOutValues == nullptr)
	{
		return;
	}

	const Vsp::Vector2 result = Vsp::Vector2(pLeft[0], pLeft[1]) + Vsp::Vector2(pRight[0], pRight[1]);
	pOutValues[0] = result.fX;
	pOutValues[1] = result.fY;
}

CSHARP_EXPORT void VspMath_Vector2Subtract(const float* pLeft, const float* pRight, float* pOutValues)
{
	if (!AreBuffersValid(pLeft, pRight) || pOutValues == nullptr)
	{
		return;
	}

	const Vsp::Vector2 result = Vsp::Vector2(pLeft[0], pLeft[1]) - Vsp::Vector2(pRight[0], pRight[1]);
	pOutValues[0] = result.fX;
	pOutValues[1] = result.fY;
}

CSHARP_EXPORT void VspMath_Vector2Scale(const float* pValue, float fScalar, float* pOutValues)
{
	if (pValue == nullptr || pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Vector2Scale was given a null buffer.");
		return;
	}

	const Vsp::Vector2 result = Vsp::Vector2(pValue[0], pValue[1]) * fScalar;
	pOutValues[0] = result.fX;
	pOutValues[1] = result.fY;
}

CSHARP_EXPORT float VspMath_Vector2Dot(const float* pLeft, const float* pRight)
{
	if (!AreBuffersValid(pLeft, pRight))
	{
		return 0.0f;
	}
	return Vsp::Vector2(pLeft[0], pLeft[1]).Dot(Vsp::Vector2(pRight[0], pRight[1]));
}

CSHARP_EXPORT float VspMath_Vector2Length(const float* pValue)
{
	if (pValue == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Vector2Length was given a null buffer.");
		return 0.0f;
	}
	return Vsp::Vector2(pValue[0], pValue[1]).GetLength();
}

CSHARP_EXPORT int32 VspMath_Vector2Normalize(const float* pValue, float* pOutValues)
{
	if (pValue == nullptr || pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Vector2Normalize was given a null buffer.");
		return 0;
	}

	const Vsp::Vector2 result = Vsp::Vector2(pValue[0], pValue[1]).GetNormalized();
	if (result == Vsp::Vector2::Zero)
	{
		// The empty value: a zero-length vector has no direction.
		pOutValues[0] = 0.0f;
		pOutValues[1] = 0.0f;
		return 0;
	}

	pOutValues[0] = result.fX;
	pOutValues[1] = result.fY;
	return 1;
}

CSHARP_EXPORT void VspMath_Vector2Lerp(const float* pFrom, const float* pTo, float fFactor, float* pOutValues)
{
	if (!AreBuffersValid(pFrom, pTo) || pOutValues == nullptr)
	{
		return;
	}

	const Vsp::Vector2 result = Vsp::Vector2::Lerp(Vsp::Vector2(pFrom[0], pFrom[1]), Vsp::Vector2(pTo[0], pTo[1]), fFactor);
	pOutValues[0] = result.fX;
	pOutValues[1] = result.fY;
}

// -------- Vector3 --------

CSHARP_EXPORT void VspMath_Vector3Add(const float* pLeft, const float* pRight, float* pOutValues)
{
	if (!AreBuffersValid(pLeft, pRight) || pOutValues == nullptr)
	{
		return;
	}
	WriteVector3(ReadVector3(pLeft) + ReadVector3(pRight), pOutValues);
}

CSHARP_EXPORT void VspMath_Vector3Subtract(const float* pLeft, const float* pRight, float* pOutValues)
{
	if (!AreBuffersValid(pLeft, pRight) || pOutValues == nullptr)
	{
		return;
	}
	WriteVector3(ReadVector3(pLeft) - ReadVector3(pRight), pOutValues);
}

CSHARP_EXPORT void VspMath_Vector3Scale(const float* pValue, float fScalar, float* pOutValues)
{
	if (pValue == nullptr || pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Vector3Scale was given a null buffer.");
		return;
	}
	WriteVector3(ReadVector3(pValue) * fScalar, pOutValues);
}

CSHARP_EXPORT float VspMath_Vector3Dot(const float* pLeft, const float* pRight)
{
	if (!AreBuffersValid(pLeft, pRight))
	{
		return 0.0f;
	}
	return Vsp::Vector3::Dot(ReadVector3(pLeft), ReadVector3(pRight));
}

CSHARP_EXPORT void VspMath_Vector3Cross(const float* pLeft, const float* pRight, float* pOutValues)
{
	if (!AreBuffersValid(pLeft, pRight) || pOutValues == nullptr)
	{
		return;
	}
	WriteVector3(Vsp::Vector3::Cross(ReadVector3(pLeft), ReadVector3(pRight)), pOutValues);
}

CSHARP_EXPORT float VspMath_Vector3Length(const float* pValue)
{
	if (pValue == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Vector3Length was given a null buffer.");
		return 0.0f;
	}
	return ReadVector3(pValue).GetLength();
}

CSHARP_EXPORT float VspMath_Vector3Distance(const float* pLeft, const float* pRight)
{
	if (!AreBuffersValid(pLeft, pRight))
	{
		return 0.0f;
	}
	return ReadVector3(pLeft).DistanceTo(ReadVector3(pRight));
}

CSHARP_EXPORT int32 VspMath_Vector3Normalize(const float* pValue, float* pOutValues)
{
	if (pValue == nullptr || pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Vector3Normalize was given a null buffer.");
		return 0;
	}

	const Vsp::Vector3 result = ReadVector3(pValue).GetNormalized();
	if (result == Vsp::Vector3::Zero)
	{
		WriteVector3(Vsp::Vector3::Zero, pOutValues);
		return 0;
	}

	WriteVector3(result, pOutValues);
	return 1;
}

CSHARP_EXPORT void VspMath_Vector3Lerp(const float* pFrom, const float* pTo, float fFactor, float* pOutValues)
{
	if (!AreBuffersValid(pFrom, pTo) || pOutValues == nullptr)
	{
		return;
	}
	WriteVector3(Vsp::Vector3::Lerp(ReadVector3(pFrom), ReadVector3(pTo), fFactor), pOutValues);
}

// The direction a yaw/pitch pair (radians) points along; yaw 0 looks down -Z.
CSHARP_EXPORT void VspMath_Vector3FromSpherical(float fYawRadians, float fPitchRadians, float* pOutValues)
{
	if (pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Vector3FromSpherical was given a null buffer.");
		return;
	}
	WriteVector3(Vsp::Vector3::FromSpherical(fYawRadians, fPitchRadians), pOutValues);
}

// -------- Vector4 --------

CSHARP_EXPORT void VspMath_Vector4Add(const float* pLeft, const float* pRight, float* pOutValues)
{
	if (!AreBuffersValid(pLeft, pRight) || pOutValues == nullptr)
	{
		return;
	}
	const Vsp::Vector4 result = Vsp::Vector4(pLeft[0], pLeft[1], pLeft[2], pLeft[3]) + Vsp::Vector4(pRight[0], pRight[1], pRight[2], pRight[3]);
	for (size_t nComponentIndex = 0; nComponentIndex < 4; ++nComponentIndex)
	{
		pOutValues[nComponentIndex] = result[nComponentIndex];
	}
}

CSHARP_EXPORT float VspMath_Vector4Dot(const float* pLeft, const float* pRight)
{
	if (!AreBuffersValid(pLeft, pRight))
	{
		return 0.0f;
	}
	return Vsp::Vector4(pLeft[0], pLeft[1], pLeft[2], pLeft[3]).Dot(Vsp::Vector4(pRight[0], pRight[1], pRight[2], pRight[3]));
}

// -------- Matrix4x4 --------

CSHARP_EXPORT void VspMath_Matrix4x4Identity(float* pOutValues)
{
	if (pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Matrix4x4Identity was given a null buffer.");
		return;
	}
	WriteMatrix4x4(Vsp::Matrix4x4::Identity(), pOutValues);
}

CSHARP_EXPORT void VspMath_Matrix4x4Multiply(const float* pLeft, const float* pRight, float* pOutValues)
{
	if (!AreBuffersValid(pLeft, pRight) || pOutValues == nullptr)
	{
		return;
	}
	WriteMatrix4x4(ReadMatrix4x4(pLeft).Multiply(ReadMatrix4x4(pRight)), pOutValues);
}

CSHARP_EXPORT int32 VspMath_Matrix4x4Inverse(const float* pValue, float* pOutValues)
{
	if (pValue == nullptr || pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Matrix4x4Inverse was given a null buffer.");
		return 0;
	}

	const Vsp::Matrix4x4 result = ReadMatrix4x4(pValue).GetInverse();
	WriteMatrix4x4(result, pOutValues);

	// A singular input produces the empty value - a zero matrix - and 0 here.
	return result.IsZero() ? 0 : 1;
}

CSHARP_EXPORT void VspMath_Matrix4x4Transpose(const float* pValue, float* pOutValues)
{
	if (pValue == nullptr || pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Matrix4x4Transpose was given a null buffer.");
		return;
	}
	WriteMatrix4x4(ReadMatrix4x4(pValue).GetTransposed(), pOutValues);
}

CSHARP_EXPORT float VspMath_Matrix4x4Determinant(const float* pValue)
{
	if (pValue == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Matrix4x4Determinant was given a null buffer.");
		return 0.0f;
	}
	return ReadMatrix4x4(pValue).GetDeterminant();
}

CSHARP_EXPORT void VspMath_Matrix4x4Trs(const float* pPosition, const float* pEulerDegrees, const float* pScale, float* pOutValues)
{
	if (pPosition == nullptr || pEulerDegrees == nullptr || pScale == nullptr || pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Matrix4x4Trs was given a null buffer.");
		return;
	}

	WriteMatrix4x4(
		Vsp::Matrix4x4::Trs(ReadVector3(pPosition), ReadVector3(pEulerDegrees), ReadVector3(pScale)),
		pOutValues);
}

CSHARP_EXPORT void VspMath_Matrix4x4RotationEuler(const float* pEulerDegrees, float* pOutValues)
{
	if (pEulerDegrees == nullptr || pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Matrix4x4RotationEuler was given a null buffer.");
		return;
	}

	const Vsp::Vector3 eulerDegrees = ReadVector3(pEulerDegrees);
	WriteMatrix4x4(Vsp::Matrix4x4::RotationEulerDegrees(eulerDegrees.fX, eulerDegrees.fY, eulerDegrees.fZ), pOutValues);
}

CSHARP_EXPORT void VspMath_Matrix4x4LookAt(const float* pEye, const float* pTarget, const float* pUp, float* pOutValues)
{
	if (pEye == nullptr || pTarget == nullptr || pUp == nullptr || pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Matrix4x4LookAt was given a null buffer.");
		return;
	}

	WriteMatrix4x4(Vsp::Matrix4x4::LookAt(ReadVector3(pEye), ReadVector3(pTarget), ReadVector3(pUp)), pOutValues);
}

CSHARP_EXPORT void VspMath_Matrix4x4Perspective(float fFieldOfViewDegrees, float fAspect, float fNear, float fFar, float* pOutValues)
{
	if (pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Matrix4x4Perspective was given a null buffer.");
		return;
	}

	constexpr float k_fDegreesToRadians = 3.14159265358979f / 180.0f;
	WriteMatrix4x4(
		Vsp::Matrix4x4::Perspective(fFieldOfViewDegrees * k_fDegreesToRadians, fAspect, fNear, fFar),
		pOutValues);
}

CSHARP_EXPORT void VspMath_Matrix4x4Orthographic(float fHalfHeight, float fAspect, float fNear, float fFar, float* pOutValues)
{
	if (pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Matrix4x4Orthographic was given a null buffer.");
		return;
	}

	WriteMatrix4x4(Vsp::Matrix4x4::Orthographic(fHalfHeight, fAspect, fNear, fFar), pOutValues);
}

CSHARP_EXPORT void VspMath_Matrix4x4OrthographicPixelSpace(float fWidth, float fHeight, float* pOutValues)
{
	if (pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Matrix4x4OrthographicPixelSpace was given a null buffer.");
		return;
	}

	WriteMatrix4x4(Vsp::Matrix4x4::OrthographicPixelSpace(fWidth, fHeight), pOutValues);
}

CSHARP_EXPORT void VspMath_Matrix4x4TransformPoint(const float* pMatrix, const float* pPoint, float* pOutValues)
{
	if (pMatrix == nullptr || pPoint == nullptr || pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Matrix4x4TransformPoint was given a null buffer.");
		return;
	}

	WriteVector3(ReadMatrix4x4(pMatrix).TransformPoint(ReadVector3(pPoint)), pOutValues);
}

CSHARP_EXPORT void VspMath_Matrix4x4TransformDirection(const float* pMatrix, const float* pDirection, float* pOutValues)
{
	if (pMatrix == nullptr || pDirection == nullptr || pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_Matrix4x4TransformDirection was given a null buffer.");
		return;
	}

	WriteVector3(ReadMatrix4x4(pMatrix).TransformDirection(ReadVector3(pDirection)), pOutValues);
}

// -------- Quaternion --------

CSHARP_EXPORT void VspMath_QuaternionFromEuler(const float* pEulerDegrees, float* pOutValues)
{
	if (pEulerDegrees == nullptr || pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_QuaternionFromEuler was given a null buffer.");
		return;
	}

	const Vsp::Quaternion result = Vsp::Quaternion::FromEulerDegrees(ReadVector3(pEulerDegrees));
	pOutValues[0] = result.fX;
	pOutValues[1] = result.fY;
	pOutValues[2] = result.fZ;
	pOutValues[3] = result.fW;
}

CSHARP_EXPORT void VspMath_QuaternionToEuler(const float* pQuaternion, float* pOutValues)
{
	if (pQuaternion == nullptr || pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_QuaternionToEuler was given a null buffer.");
		return;
	}

	const Vsp::Quaternion value(pQuaternion[0], pQuaternion[1], pQuaternion[2], pQuaternion[3]);
	WriteVector3(value.ToEulerDegrees(), pOutValues);
}

CSHARP_EXPORT void VspMath_QuaternionMultiply(const float* pLeft, const float* pRight, float* pOutValues)
{
	if (!AreBuffersValid(pLeft, pRight) || pOutValues == nullptr)
	{
		return;
	}

	const Vsp::Quaternion result = Vsp::Quaternion(pLeft[0], pLeft[1], pLeft[2], pLeft[3])
		.Multiply(Vsp::Quaternion(pRight[0], pRight[1], pRight[2], pRight[3]));

	pOutValues[0] = result.fX;
	pOutValues[1] = result.fY;
	pOutValues[2] = result.fZ;
	pOutValues[3] = result.fW;
}

CSHARP_EXPORT void VspMath_QuaternionRotateVector(const float* pQuaternion, const float* pDirection, float* pOutValues)
{
	if (pQuaternion == nullptr || pDirection == nullptr || pOutValues == nullptr)
	{
		VSP_LOG_ERROR(kLogTag, "VspMath_QuaternionRotateVector was given a null buffer.");
		return;
	}

	const Vsp::Quaternion value(pQuaternion[0], pQuaternion[1], pQuaternion[2], pQuaternion[3]);
	WriteVector3(value.RotateVector(ReadVector3(pDirection)), pOutValues);
}

CSHARP_EXPORT void VspMath_QuaternionSlerp(const float* pFrom, const float* pTo, float fFactor, float* pOutValues)
{
	if (!AreBuffersValid(pFrom, pTo) || pOutValues == nullptr)
	{
		return;
	}

	const Vsp::Quaternion result = Vsp::Quaternion::Slerp(
		Vsp::Quaternion(pFrom[0], pFrom[1], pFrom[2], pFrom[3]),
		Vsp::Quaternion(pTo[0], pTo[1], pTo[2], pTo[3]),
		fFactor);

	pOutValues[0] = result.fX;
	pOutValues[1] = result.fY;
	pOutValues[2] = result.fZ;
	pOutValues[3] = result.fW;
}

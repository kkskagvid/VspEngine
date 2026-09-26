#include "RuntimePCH.h"

#include "Core/Diagnostics/ErrorHandling.h"
#include "Math/Matrix4x4.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "Math";

	// -------------------------------------------------------------------------
	// Rotations
	// -------------------------------------------------------------------------

	Matrix4x4 Matrix4x4::RotationAxis(const Vector3& Axis, float fRadians)
	{
		// A zero axis cannot describe a rotation, so the operation reports the
		// empty value - no rotation at all - after logging why.
		const float fAxisLengthSquared = Axis.GetLengthSquared();
		if (fAxisLengthSquared <= 0.0f)
		{
			VSP_LOG_ERROR(kLogTag, "Matrix4x4::RotationAxis got a zero-length axis; the identity is returned.");
			return Identity();
		}

		const float fInverseLength = 1.0f / std::sqrt(fAxisLengthSquared);
		const float fX = Axis.fX * fInverseLength;
		const float fY = Axis.fY * fInverseLength;
		const float fZ = Axis.fZ * fInverseLength;

		const float fCosine = std::cos(fRadians);
		const float fSine = std::sin(fRadians);
		const float fOneMinusCosine = 1.0f - fCosine;

		Matrix4x4 result = Identity();
		result.SetElement(0, 0, (fCosine + (fX * fX * fOneMinusCosine)));
		result.SetElement(0, 1, ((fX * fY * fOneMinusCosine) - (fZ * fSine)));
		result.SetElement(0, 2, ((fX * fZ * fOneMinusCosine) + (fY * fSine)));

		result.SetElement(1, 0, ((fY * fX * fOneMinusCosine) + (fZ * fSine)));
		result.SetElement(1, 1, (fCosine + (fY * fY * fOneMinusCosine)));
		result.SetElement(1, 2, ((fY * fZ * fOneMinusCosine) - (fX * fSine)));

		result.SetElement(2, 0, ((fZ * fX * fOneMinusCosine) - (fY * fSine)));
		result.SetElement(2, 1, ((fZ * fY * fOneMinusCosine) + (fX * fSine)));
		result.SetElement(2, 2, (fCosine + (fZ * fZ * fOneMinusCosine)));
		return result;
	}

	Matrix4x4 Matrix4x4::RotationEulerDegrees(float fDegreesX, float fDegreesY, float fDegreesZ)
	{
		constexpr float k_fDegreesToRadians = 3.14159265358979f / 180.0f;

		// The engine's order: Z first, then Y, then X - the product Rx * Ry * Rz
		// that Transform::BuildLocalMatrix and Transform::GetWorldRotation
		// agree on.
		const Matrix4x4 rotationX = RotationX(fDegreesX * k_fDegreesToRadians);
		const Matrix4x4 rotationY = RotationY(fDegreesY * k_fDegreesToRadians);
		const Matrix4x4 rotationZ = RotationZ(fDegreesZ * k_fDegreesToRadians);
		return rotationX.Multiply(rotationY).Multiply(rotationZ);
	}

	Matrix4x4 Matrix4x4::Trs(const Vector3& Position, const Vector3& EulerDegrees, const Vector3& Scaling)
	{
		// Scale, then rotate, then translate.
		return Translation(Position)
			.Multiply(RotationEulerDegrees(EulerDegrees.fX, EulerDegrees.fY, EulerDegrees.fZ))
			.Multiply(Scale(Scaling));
	}

	// -------------------------------------------------------------------------
	// Camera matrices
	// -------------------------------------------------------------------------

	Matrix4x4 Matrix4x4::LookAt(const Vector3& Eye, const Vector3& Target, const Vector3& Up)
	{
		const Vector3 forward = (Target - Eye).GetNormalized();
		if (forward.GetLengthSquared() <= 0.0f)
		{
			VSP_LOG_ERROR(kLogTag, "Matrix4x4::LookAt got an eye and a target at the same place; the identity is returned.");
			return Identity();
		}

		// A view direction parallel to Up cannot define a roll, so the world's
		// +Y is the fallback axis (what a camera rig does when it looks
		// straight down).
		Vector3 safeUp = Up.GetNormalized();
		Vector3 right = Vector3::Cross(forward, safeUp);
		if (right.GetLengthSquared() <= 1e-8f)
		{
			safeUp = (forward.fY > 0.99f || forward.fY < -0.99f) ? Vector3(0.0f, 0.0f, 1.0f) : Vector3(0.0f, 1.0f, 0.0f);
			right = Vector3::Cross(forward, safeUp);
		}
		right = right.GetNormalized();
		const Vector3 cameraUp = Vector3::Cross(right, forward);

		Matrix4x4 result = Identity();
		result.SetElement(0, 0, right.fX);
		result.SetElement(0, 1, right.fY);
		result.SetElement(0, 2, right.fZ);
		result.SetElement(0, 3, -Vector3::Dot(right, Eye));

		result.SetElement(1, 0, cameraUp.fX);
		result.SetElement(1, 1, cameraUp.fY);
		result.SetElement(1, 2, cameraUp.fZ);
		result.SetElement(1, 3, -Vector3::Dot(cameraUp, Eye));

		// The camera looks down its own -Z, so the third row is -forward.
		result.SetElement(2, 0, -forward.fX);
		result.SetElement(2, 1, -forward.fY);
		result.SetElement(2, 2, -forward.fZ);
		result.SetElement(2, 3, Vector3::Dot(forward, Eye));
		return result;
	}

	Matrix4x4 Matrix4x4::Perspective(float fFieldOfViewRadians, float fAspect, float fNear, float fFar)
	{
		if (fAspect <= 0.0f || fNear <= 0.0f || fFar <= fNear)
		{
			VSP_LOG_ERROR(kLogTag, "Matrix4x4::Perspective got aspect {}, near {} and far {}; the identity is returned.",
				fAspect, fNear, fFar);
			return Identity();
		}

		const float fFocalScale = 1.0f / std::tan(fFieldOfViewRadians * 0.5f);

		Matrix4x4 result;
		result.SetElement(0, 0, fFocalScale / fAspect);
		// Vulkan clip space has Y pointing down, so the view's Y is flipped
		// here; world +Y then ends up at the top of the screen.
		result.SetElement(1, 1, -fFocalScale);
		result.SetElement(2, 2, fFar / (fNear - fFar));
		result.SetElement(2, 3, (fNear * fFar) / (fNear - fFar));
		result.SetElement(3, 2, -1.0f);
		return result;
	}

	Matrix4x4 Matrix4x4::Orthographic(float fHalfHeight, float fAspect, float fNear, float fFar)
	{
		if (fHalfHeight <= 0.0f || fAspect <= 0.0f || fFar <= fNear)
		{
			VSP_LOG_ERROR(kLogTag, "Matrix4x4::Orthographic got half height {}, aspect {}, near {} and far {}; the identity is returned.",
				fHalfHeight, fAspect, fNear, fFar);
			return Identity();
		}

		const float fHalfWidth = fHalfHeight * fAspect;

		Matrix4x4 result;
		result.SetElement(0, 0, 1.0f / fHalfWidth);
		result.SetElement(1, 1, -1.0f / fHalfHeight);   // Vulkan's Y flip.
		result.SetElement(2, 2, 1.0f / (fNear - fFar));
		result.SetElement(2, 3, fNear / (fNear - fFar));
		result.SetElement(3, 3, 1.0f);
		return result;
	}

	Matrix4x4 Matrix4x4::OrthographicPixelSpace(float fWidth, float fHeight)
	{
		if (fWidth <= 0.0f || fHeight <= 0.0f)
		{
			VSP_LOG_ERROR(kLogTag, "Matrix4x4::OrthographicPixelSpace got {} x {}; the identity is returned.", fWidth, fHeight);
			return Identity();
		}

		// [0, width] x [0, height] with the origin at the top-left corner maps
		// onto Vulkan's clip box; the depth is fixed at 0 (the near plane), so a
		// UI vertex is in front of everything the 3D scene wrote.
		Matrix4x4 result = Identity();
		result.SetElement(0, 0, 2.0f / fWidth);
		result.SetElement(1, 1, 2.0f / fHeight);
		result.SetElement(0, 3, -1.0f);
		result.SetElement(1, 3, -1.0f);
		result.SetElement(2, 2, 0.0f);
		result.SetElement(2, 3, 0.0f);
		return result;
	}

	// -------------------------------------------------------------------------
	// Algebra
	// -------------------------------------------------------------------------

	Matrix4x4 Matrix4x4::Multiply(const Matrix4x4& Other) const
	{
		Matrix4x4 result;
		for (size_t nRow = 0; nRow < 4; ++nRow)
		{
			for (size_t nColumn = 0; nColumn < 4; ++nColumn)
			{
				float fSum = 0.0f;
				for (size_t nIndex = 0; nIndex < 4; ++nIndex)
				{
					fSum += GetElement(nRow, nIndex) * Other.GetElement(nIndex, nColumn);
				}
				result.SetElement(nRow, nColumn, fSum);
			}
		}
		return result;
	}

	Vector3 Matrix4x4::TransformPoint(const Vector3& Point) const
	{
		const Vector4 transformed = MultiplyVector4(Vector4(Point, 1.0f));

		// An affine matrix leaves w at 1; a projection matrix divides by it.
		const float fAbsoluteW = (transformed.fW < 0.0f) ? -transformed.fW : transformed.fW;
		if (fAbsoluteW <= 1e-8f)
		{
			return Vector3(transformed.fX, transformed.fY, transformed.fZ);
		}
		return Vector3(transformed.fX / transformed.fW, transformed.fY / transformed.fW, transformed.fZ / transformed.fW);
	}

	float Matrix4x4::GetDeterminant() const
	{
		const float fM00 = GetElement(0, 0); const float fM01 = GetElement(0, 1); const float fM02 = GetElement(0, 2); const float fM03 = GetElement(0, 3);
		const float fM10 = GetElement(1, 0); const float fM11 = GetElement(1, 1); const float fM12 = GetElement(1, 2); const float fM13 = GetElement(1, 3);
		const float fM20 = GetElement(2, 0); const float fM21 = GetElement(2, 1); const float fM22 = GetElement(2, 2); const float fM23 = GetElement(2, 3);
		const float fM30 = GetElement(3, 0); const float fM31 = GetElement(3, 1); const float fM32 = GetElement(3, 2); const float fM33 = GetElement(3, 3);

		const float fSub00 = (fM22 * fM33) - (fM23 * fM32);
		const float fSub01 = (fM21 * fM33) - (fM23 * fM31);
		const float fSub02 = (fM21 * fM32) - (fM22 * fM31);
		const float fSub03 = (fM20 * fM33) - (fM23 * fM30);
		const float fSub04 = (fM20 * fM32) - (fM22 * fM30);
		const float fSub05 = (fM20 * fM31) - (fM21 * fM30);

		return
			(fM00 * ((fM11 * fSub00) - (fM12 * fSub01) + (fM13 * fSub02))) -
			(fM01 * ((fM10 * fSub00) - (fM12 * fSub03) + (fM13 * fSub04))) +
			(fM02 * ((fM10 * fSub01) - (fM11 * fSub03) + (fM13 * fSub05))) -
			(fM03 * ((fM10 * fSub02) - (fM11 * fSub04) + (fM12 * fSub05)));
	}

	Matrix4x4 Matrix4x4::GetInverse() const
	{
		// The classic cofactor inverse (the MESA/Graphics Gems formulation),
		// written against the column-major layout the engine stores. Every
		// "fMrc" name below is the element in row r, column c of this matrix.
		const float fM00 = GetElement(0, 0); const float fM01 = GetElement(0, 1); const float fM02 = GetElement(0, 2); const float fM03 = GetElement(0, 3);
		const float fM10 = GetElement(1, 0); const float fM11 = GetElement(1, 1); const float fM12 = GetElement(1, 2); const float fM13 = GetElement(1, 3);
		const float fM20 = GetElement(2, 0); const float fM21 = GetElement(2, 1); const float fM22 = GetElement(2, 2); const float fM23 = GetElement(2, 3);
		const float fM30 = GetElement(3, 0); const float fM31 = GetElement(3, 1); const float fM32 = GetElement(3, 2); const float fM33 = GetElement(3, 3);

		float fInverse[16] = {};

		// Column 0.
		fInverse[0] = (fM11 * fM22 * fM33) - (fM11 * fM23 * fM32) - (fM21 * fM12 * fM33) + (fM21 * fM13 * fM32) + (fM31 * fM12 * fM23) - (fM31 * fM13 * fM22);
		fInverse[4] = -((fM10 * fM22 * fM33) - (fM10 * fM23 * fM32) - (fM20 * fM12 * fM33) + (fM20 * fM13 * fM32) + (fM30 * fM12 * fM23) - (fM30 * fM13 * fM22));
		fInverse[8] = (fM10 * fM21 * fM33) - (fM10 * fM23 * fM31) - (fM20 * fM11 * fM33) + (fM20 * fM13 * fM31) + (fM30 * fM11 * fM23) - (fM30 * fM13 * fM21);
		fInverse[12] = -((fM10 * fM21 * fM32) - (fM10 * fM22 * fM31) - (fM20 * fM11 * fM32) + (fM20 * fM12 * fM31) + (fM30 * fM11 * fM22) - (fM30 * fM12 * fM21));

		// Column 1.
		fInverse[1] = -((fM01 * fM22 * fM33) - (fM01 * fM23 * fM32) - (fM21 * fM02 * fM33) + (fM21 * fM03 * fM32) + (fM31 * fM02 * fM23) - (fM31 * fM03 * fM22));
		fInverse[5] = (fM00 * fM22 * fM33) - (fM00 * fM23 * fM32) - (fM20 * fM02 * fM33) + (fM20 * fM03 * fM32) + (fM30 * fM02 * fM23) - (fM30 * fM03 * fM22);
		fInverse[9] = -((fM00 * fM21 * fM33) - (fM00 * fM23 * fM31) - (fM20 * fM01 * fM33) + (fM20 * fM03 * fM31) + (fM30 * fM01 * fM23) - (fM30 * fM03 * fM21));
		fInverse[13] = (fM00 * fM21 * fM32) - (fM00 * fM22 * fM31) - (fM20 * fM01 * fM32) + (fM20 * fM02 * fM31) + (fM30 * fM01 * fM22) - (fM30 * fM02 * fM21);

		// Column 2.
		fInverse[2] = (fM01 * fM12 * fM33) - (fM01 * fM13 * fM32) - (fM11 * fM02 * fM33) + (fM11 * fM03 * fM32) + (fM31 * fM02 * fM13) - (fM31 * fM03 * fM12);
		fInverse[6] = -((fM00 * fM12 * fM33) - (fM00 * fM13 * fM32) - (fM10 * fM02 * fM33) + (fM10 * fM03 * fM32) + (fM30 * fM02 * fM13) - (fM30 * fM03 * fM12));
		fInverse[10] = (fM00 * fM11 * fM33) - (fM00 * fM13 * fM31) - (fM10 * fM01 * fM33) + (fM10 * fM03 * fM31) + (fM30 * fM01 * fM13) - (fM30 * fM03 * fM11);
		fInverse[14] = -((fM00 * fM11 * fM32) - (fM00 * fM12 * fM31) - (fM10 * fM01 * fM32) + (fM10 * fM02 * fM31) + (fM30 * fM01 * fM12) - (fM30 * fM02 * fM11));

		// Column 3.
		fInverse[3] = -((fM01 * fM12 * fM23) - (fM01 * fM13 * fM22) - (fM11 * fM02 * fM23) + (fM11 * fM03 * fM22) + (fM21 * fM02 * fM13) - (fM21 * fM03 * fM12));
		fInverse[7] = (fM00 * fM12 * fM23) - (fM00 * fM13 * fM22) - (fM10 * fM02 * fM23) + (fM10 * fM03 * fM22) + (fM20 * fM02 * fM13) - (fM20 * fM03 * fM12);
		fInverse[11] = -((fM00 * fM11 * fM23) - (fM00 * fM13 * fM21) - (fM10 * fM01 * fM23) + (fM10 * fM03 * fM21) + (fM20 * fM01 * fM13) - (fM20 * fM03 * fM11));
		fInverse[15] = (fM00 * fM11 * fM22) - (fM00 * fM12 * fM21) - (fM10 * fM01 * fM22) + (fM10 * fM02 * fM21) + (fM20 * fM01 * fM12) - (fM20 * fM02 * fM11);

		// The determinant falls out of the first row dotted with the first
		// column of the cofactor matrix.
		const float fDeterminant = (fM00 * fInverse[0]) + (fM01 * fInverse[4]) + (fM02 * fInverse[8]) + (fM03 * fInverse[12]);

		// A singular matrix has no inverse, so the operation reports the empty
		// value - a zero matrix - after logging why. Callers test with IsZero().
		const float fAbsoluteDeterminant = (fDeterminant < 0.0f) ? -fDeterminant : fDeterminant;
		if (fAbsoluteDeterminant <= 1e-12f)
		{
			VSP_LOG_ERROR(kLogTag, "Matrix4x4::GetInverse got a singular matrix (determinant {}); a zero matrix is returned.", fDeterminant);
			return Zero();
		}

		const float fInverseDeterminant = 1.0f / fDeterminant;

		Matrix4x4 result;
		for (size_t nElementIndex = 0; nElementIndex < 16; ++nElementIndex)
		{
			result.fElements[nElementIndex] = fInverse[nElementIndex] * fInverseDeterminant;
		}
		return result;
	}

	bool Matrix4x4::IsZero() const
	{
		for (size_t nElementIndex = 0; nElementIndex < 16; ++nElementIndex)
		{
			if (fElements[nElementIndex] != 0.0f)
			{
				return false;
			}
		}
		return true;
	}
}
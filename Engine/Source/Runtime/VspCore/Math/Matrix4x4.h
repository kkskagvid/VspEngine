#pragma once

#include <cmath>

#include "Core/Core.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// Matrix4x4
	// -------------------------------------------------------------------------
	// Column-major 4x4 float matrix - the layout glm produces, the layout Vulkan
	// reads and the layout a shader's column_major block member expects.
	//
	// The element in row r, column c lives at fElements[c * 4 + r], so the
	// translation of an affine transform sits at fElements[12..14], the same
	// places Transform writes it.
	//
	// Conventions, identical to Camera and Transform:
	//   * right-handed, Y up,
	//   * an object looks down its local -Z,
	//   * clip space is Vulkan's: X right, Y down, depth 0..1.
	//
	// Nothing here throws; an operation that cannot be carried out (inverting a
	// singular matrix, normalizing a zero vector) produces the empty value -
	// a zero matrix or a zero vector - after an entry has been logged.
	// -------------------------------------------------------------------------
	struct Matrix4x4
	{
		// Element (row, column) at column * 4 + row.
		float fElements[16] = {};

		// -------- Construction --------
		constexpr Matrix4x4() = default;

		// Reads the sixteen elements in COLUMN-MAJOR order (element 0 is row 0
		// of column 0), which is the order the engine stores and uploads.
		explicit constexpr Matrix4x4(const float* pColumnMajorElements)
		{
			for (size_t nElementIndex = 0; nElementIndex < 16; ++nElementIndex)
			{
				fElements[nElementIndex] = pColumnMajorElements[nElementIndex];
			}
		}

		// -------- Element access --------
		constexpr float GetElement(size_t nRow, size_t nColumn) const { return fElements[(nColumn * 4) + nRow]; }
		constexpr void SetElement(size_t nRow, size_t nColumn, float fValue) { fElements[(nColumn * 4) + nRow] = fValue; }

		constexpr Vector3 GetColumn(size_t nColumn) const
		{
			return Vector3(fElements[(nColumn * 4) + 0], fElements[(nColumn * 4) + 1], fElements[(nColumn * 4) + 2]);
		}

		constexpr Vector4 GetColumn4(size_t nColumn) const
		{
			return Vector4(
				fElements[(nColumn * 4) + 0], fElements[(nColumn * 4) + 1],
				fElements[(nColumn * 4) + 2], fElements[(nColumn * 4) + 3]);
		}

		// The translation an affine transform carries.
		constexpr Vector3 GetTranslation() const
		{
			return Vector3(fElements[12], fElements[13], fElements[14]);
		}

		// -------- Ready-made values --------
		static constexpr Matrix4x4 Identity()
		{
			Matrix4x4 result;
			result.fElements[0] = 1.0f;
			result.fElements[5] = 1.0f;
			result.fElements[10] = 1.0f;
			result.fElements[15] = 1.0f;
			return result;
		}

		static constexpr Matrix4x4 Zero()
		{
			return Matrix4x4();
		}

		// -------- Affine building blocks --------
		static constexpr Matrix4x4 Translation(const Vector3& Offset)
		{
			Matrix4x4 result = Identity();
			result.fElements[12] = Offset.fX;
			result.fElements[13] = Offset.fY;
			result.fElements[14] = Offset.fZ;
			return result;
		}

		static constexpr Matrix4x4 Scale(const Vector3& Scaling)
		{
			Matrix4x4 result = Identity();
			result.fElements[0] = Scaling.fX;
			result.fElements[5] = Scaling.fY;
			result.fElements[10] = Scaling.fZ;
			return result;
		}

		// -------- Rotations (radians, right-handed, counter-clockwise about the
		// axis when looked at from its positive end) --------
		static Matrix4x4 RotationX(float fRadians)
		{
			const float fCosine = std::cos(fRadians);
			const float fSine = std::sin(fRadians);

			Matrix4x4 result = Identity();
			result.fElements[5] = fCosine;
			result.fElements[6] = fSine;
			result.fElements[9] = -fSine;
			result.fElements[10] = fCosine;
			return result;
		}

		static Matrix4x4 RotationY(float fRadians)
		{
			const float fCosine = std::cos(fRadians);
			const float fSine = std::sin(fRadians);

			Matrix4x4 result = Identity();
			result.fElements[0] = fCosine;
			result.fElements[2] = -fSine;
			result.fElements[8] = fSine;
			result.fElements[10] = fCosine;
			return result;
		}

		static Matrix4x4 RotationZ(float fRadians)
		{
			const float fCosine = std::cos(fRadians);
			const float fSine = std::sin(fRadians);

			Matrix4x4 result = Identity();
			result.fElements[0] = fCosine;
			result.fElements[1] = fSine;
			result.fElements[4] = -fSine;
			result.fElements[5] = fCosine;
			return result;
		}

		// Turn of fRadians about an arbitrary axis; a zero axis produces the
		// identity, which leaves the object where it was.
		static Matrix4x4 RotationAxis(const Vector3& Axis, float fRadians);

		// The engine's Euler convention: degrees, applied Z then Y then X, i.e.
		// the product Rx * Ry * Rz. This is exactly what Transform stores and
		// what Transform::GetWorldRotation reads back out of a matrix, so the
		// two agree in both directions.
		static Matrix4x4 RotationEulerDegrees(float fDegreesX, float fDegreesY, float fDegreesZ);

		// Translation * rotation * scale, the local transform of an object.
		static Matrix4x4 Trs(const Vector3& Position, const Vector3& EulerDegrees, const Vector3& Scaling);

		// -------- Camera matrices (the engine's clip convention) --------
		// A right-handed view matrix that looks from Eye towards Target, with
		// the world's Up deciding the roll. A camera with this view looks down
		// its own -Z.
		static Matrix4x4 LookAt(const Vector3& Eye, const Vector3& Target, const Vector3& Up);

		// Perspective projection into VULKAN clip space (X right, Y down,
		// depth 0..1) for a camera looking down -Z. Same shape Camera produces.
		static Matrix4x4 Perspective(float fFieldOfViewRadians, float fAspect, float fNear, float fFar);

		// Orthographic projection into VULKAN clip space, given the half height
		// the camera sees. Same shape Camera produces for its orthographic mode.
		static Matrix4x4 Orthographic(float fHalfHeight, float fAspect, float fNear, float fFar);

		// An orthographic projection for pixel-space UI work: the box is
		// [0, width] x [0, height], with the origin at the TOP-LEFT corner and
		// +Y pointing down the screen - the coordinate system a layout works in.
		// Every UI vertex lands at depth 0, i.e. on the near plane, so UI drawn
		// with LessOrEqual depth testing stays in front of the 3D scene.
		static Matrix4x4 OrthographicPixelSpace(float fWidth, float fHeight);

		// -------- Algebra --------
		// This * Other, so this is applied after Other.
		Matrix4x4 Multiply(const Matrix4x4& Other) const;

		constexpr Vector4 MultiplyVector4(const Vector4& Value) const
		{
			return Vector4(
				(fElements[0] * Value.fX) + (fElements[4] * Value.fY) + (fElements[8] * Value.fZ) + (fElements[12] * Value.fW),
				(fElements[1] * Value.fX) + (fElements[5] * Value.fY) + (fElements[9] * Value.fZ) + (fElements[13] * Value.fW),
				(fElements[2] * Value.fX) + (fElements[6] * Value.fY) + (fElements[10] * Value.fZ) + (fElements[14] * Value.fW),
				(fElements[3] * Value.fX) + (fElements[7] * Value.fY) + (fElements[11] * Value.fZ) + (fElements[15] * Value.fW));
		}

		// A point (w = 1) taken through the matrix, with the perspective divide.
		Vector3 TransformPoint(const Vector3& Point) const;

		// A direction (w = 0) taken through the matrix: the translation is not
		// applied.
		constexpr Vector3 TransformDirection(const Vector3& Direction) const
		{
			return Vector3(
				(fElements[0] * Direction.fX) + (fElements[4] * Direction.fY) + (fElements[8] * Direction.fZ),
				(fElements[1] * Direction.fX) + (fElements[5] * Direction.fY) + (fElements[9] * Direction.fZ),
				(fElements[2] * Direction.fX) + (fElements[6] * Direction.fY) + (fElements[10] * Direction.fZ));
		}

		constexpr Matrix4x4 GetTransposed() const
		{
			Matrix4x4 result;
			for (size_t nRow = 0; nRow < 4; ++nRow)
			{
				for (size_t nColumn = 0; nColumn < 4; ++nColumn)
				{
					result.SetElement(nRow, nColumn, GetElement(nColumn, nRow));
				}
			}
			return result;
		}

		float GetDeterminant() const;

		// The inverse, or a ZERO matrix when the matrix is singular (the reason
		// is logged) - a caller can test the result with IsZero() instead of
		// having to deal with a failed operation.
		Matrix4x4 GetInverse() const;

		bool IsZero() const;

		constexpr bool operator==(const Matrix4x4& Other) const
		{
			for (size_t nElementIndex = 0; nElementIndex < 16; ++nElementIndex)
			{
				if (fElements[nElementIndex] != Other.fElements[nElementIndex])
				{
					return false;
				}
			}
			return true;
		}

		constexpr bool operator!=(const Matrix4x4& Other) const { return !(*this == Other); }
	};

	// -------- Free operators --------
	inline Matrix4x4 operator*(const Matrix4x4& Left, const Matrix4x4& Right)
	{
		return Left.Multiply(Right);
	}

	inline constexpr Vector4 operator*(const Matrix4x4& Left, const Vector4& Right)
	{
		return Left.MultiplyVector4(Right);
	}
}

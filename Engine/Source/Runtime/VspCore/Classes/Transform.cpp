#include "RuntimePCH.h"

#include <cmath>
#include <cstring>

#include "Classes/Scene.h"
#include "Classes/Transform.h"

namespace Vsp
{
	namespace
	{
		// An identity column-major 4x4 matrix.
		void BuildIdentityMatrix(float* pOutMatrix)
		{
			for (uint32 uElementIndex = 0; uElementIndex < 16; ++uElementIndex)
			{
				pOutMatrix[uElementIndex] = 0.0f;
			}
			pOutMatrix[0] = 1.0f;
			pOutMatrix[5] = 1.0f;
			pOutMatrix[10] = 1.0f;
			pOutMatrix[15] = 1.0f;
		}

		// out = left * right, both column-major 4x4.
		void MultiplyMatrices(const float* pLeftMatrix, const float* pRightMatrix, float* pOutMatrix)
		{
			float result[16] = {};
			for (uint32 uColumn = 0; uColumn < 4; ++uColumn)
			{
				for (uint32 uRow = 0; uRow < 4; ++uRow)
				{
					float fValue = 0.0f;
					for (uint32 uIndex = 0; uIndex < 4; ++uIndex)
					{
						// Column-major: element (row, column) sits at column * 4 + row.
						fValue += pLeftMatrix[(uIndex * 4) + uRow] * pRightMatrix[(uColumn * 4) + uIndex];
					}
					result[(uColumn * 4) + uRow] = fValue;
				}
			}
			memcpy(pOutMatrix, result, sizeof(result));
		}
	}

	void Transform::SetOwnerGameObjectHandle(NativeObjectHandle uGameObjectHandle)
	{
		m_uOwnerGameObjectHandle = uGameObjectHandle;
	}

	// -------------------------------------------------------------------------
	// Local transform
	// -------------------------------------------------------------------------

	void Transform::GetLocalPosition(float& outPositionX, float& outPositionY, float& outPositionZ) const
	{
		outPositionX = m_fLocalPositionX;
		outPositionY = m_fLocalPositionY;
		outPositionZ = m_fLocalPositionZ;
	}

	void Transform::SetLocalPosition(float fPositionX, float fPositionY, float fPositionZ)
	{
		m_fLocalPositionX = fPositionX;
		m_fLocalPositionY = fPositionY;
		m_fLocalPositionZ = fPositionZ;
		m_bIsDirty = true;
		MarkWorldDirty();
	}

	void Transform::GetLocalRotation(float& outRotationX, float& outRotationY, float& outRotationZ) const
	{
		outRotationX = m_fLocalRotationX;
		outRotationY = m_fLocalRotationY;
		outRotationZ = m_fLocalRotationZ;
	}

	void Transform::SetLocalRotation(float fRotationX, float fRotationY, float fRotationZ)
	{
		m_fLocalRotationX = fRotationX;
		m_fLocalRotationY = fRotationY;
		m_fLocalRotationZ = fRotationZ;
		m_bIsDirty = true;
		MarkWorldDirty();
	}

	void Transform::GetLocalScale(float& outScaleX, float& outScaleY, float& outScaleZ) const
	{
		outScaleX = m_fLocalScaleX;
		outScaleY = m_fLocalScaleY;
		outScaleZ = m_fLocalScaleZ;
	}

	void Transform::SetLocalScale(float fScaleX, float fScaleY, float fScaleZ)
	{
		m_fLocalScaleX = fScaleX;
		m_fLocalScaleY = fScaleY;
		m_fLocalScaleZ = fScaleZ;
		m_bIsDirty = true;
		MarkWorldDirty();
	}

	// -------------------------------------------------------------------------
	// Local matrix
	// -------------------------------------------------------------------------

	void Transform::BuildLocalMatrix(float* pOutMatrix) const
	{
		const float fDegreesToRadians = 0.01745329251994329577f;

		const float fRotationX = m_fLocalRotationX * fDegreesToRadians;
		const float fRotationY = m_fLocalRotationY * fDegreesToRadians;
		const float fRotationZ = m_fLocalRotationZ * fDegreesToRadians;

		const float fSinX = sinf(fRotationX);
		const float fCosX = cosf(fRotationX);
		const float fSinY = sinf(fRotationY);
		const float fCosY = cosf(fRotationY);
		const float fSinZ = sinf(fRotationZ);
		const float fCosZ = cosf(fRotationZ);

		// Rotation Z, then Y, then X - the same order the builtin HLSL
		// VspRotationMatrixFromEuler uses, so a shader and the engine agree on
		// where an object points.
		//
		// The matrix is the rotation matrix with its axes scaled, which is the
		// same as scaling first and rotating after.
		const float fScaledAxisXx = (fCosY * fCosZ) * m_fLocalScaleX;
		const float fScaledAxisXy = ((fCosX * fSinZ) + (fSinX * fSinY * fCosZ)) * m_fLocalScaleX;
		const float fScaledAxisXz = ((fSinX * fSinZ) - (fCosX * fSinY * fCosZ)) * m_fLocalScaleX;

		const float fScaledAxisYx = (-fCosY * fSinZ) * m_fLocalScaleY;
		const float fScaledAxisYy = ((fCosX * fCosZ) - (fSinX * fSinY * fSinZ)) * m_fLocalScaleY;
		const float fScaledAxisYz = ((fSinX * fCosZ) + (fCosX * fSinY * fSinZ)) * m_fLocalScaleY;

		const float fScaledAxisZx = fSinY * m_fLocalScaleZ;
		const float fScaledAxisZy = (-fSinX * fCosY) * m_fLocalScaleZ;
		const float fScaledAxisZz = (fCosX * fCosY) * m_fLocalScaleZ;

		// Column-major: the first three entries of a column are the axis.
		pOutMatrix[0] = fScaledAxisXx;
		pOutMatrix[1] = fScaledAxisXy;
		pOutMatrix[2] = fScaledAxisXz;
		pOutMatrix[3] = 0.0f;

		pOutMatrix[4] = fScaledAxisYx;
		pOutMatrix[5] = fScaledAxisYy;
		pOutMatrix[6] = fScaledAxisYz;
		pOutMatrix[7] = 0.0f;

		pOutMatrix[8] = fScaledAxisZx;
		pOutMatrix[9] = fScaledAxisZy;
		pOutMatrix[10] = fScaledAxisZz;
		pOutMatrix[11] = 0.0f;

		pOutMatrix[12] = m_fLocalPositionX;
		pOutMatrix[13] = m_fLocalPositionY;
		pOutMatrix[14] = m_fLocalPositionZ;
		pOutMatrix[15] = 1.0f;
	}

	void Transform::GetLocalMatrix(float* pOutMatrix) const
	{
		if (pOutMatrix == nullptr)
		{
			return;
		}
		BuildLocalMatrix(pOutMatrix);
	}

	// -------------------------------------------------------------------------
	// World transform
	// -------------------------------------------------------------------------

	Transform* Transform::ResolveParent() const
	{
		if (m_uParentTransformHandle == k_nInvalidObjectHandle)
		{
			return nullptr;
		}
		return Scene::Get().FindTransform(m_uParentTransformHandle);
	}

	void Transform::MarkWorldDirty()
	{
		m_bWorldIsDirty = true;

		// Everything below this transform moves with it.
		for (size_t nChildIndex = 0; nChildIndex < m_ChildTransformHandles.GetSize(); ++nChildIndex)
		{
			Transform* pChild = Scene::Get().FindTransform(m_ChildTransformHandles[nChildIndex]);
			if (pChild != nullptr)
			{
				pChild->MarkWorldDirty();
			}
		}
	}

	void Transform::EnsureWorldMatrix()
	{
		if (!m_bWorldIsDirty)
		{
			return;
		}

		float localMatrix[16] = {};
		BuildLocalMatrix(localMatrix);

		Transform* pParent = ResolveParent();
		if (pParent == nullptr)
		{
			// A root object is at its local place; a parent that was released
			// stops being a parent, so the stale handle is dropped here.
			m_uParentTransformHandle = k_nInvalidObjectHandle;
			memcpy(m_WorldMatrix, localMatrix, sizeof(m_WorldMatrix));
		}
		else
		{
			pParent->EnsureWorldMatrix();
			MultiplyMatrices(pParent->m_WorldMatrix, localMatrix, m_WorldMatrix);
		}

		m_bWorldIsDirty = false;
	}

	void Transform::GetWorldMatrix(float* pOutMatrix)
	{
		if (pOutMatrix == nullptr)
		{
			return;
		}

		EnsureWorldMatrix();
		memcpy(pOutMatrix, m_WorldMatrix, sizeof(m_WorldMatrix));
	}

	void Transform::GetWorldToLocalMatrix(float* pOutMatrix)
	{
		if (pOutMatrix == nullptr)
		{
			return;
		}

		EnsureWorldMatrix();

		// The world matrix is an affine transform (rotation, scale,
		// translation), so its inverse is built from the three axes rather than
		// with a general 4x4 inverse: cheap, and exact when the axes are
		// perpendicular.
		const float* pMatrix = m_WorldMatrix;
		const float fAxisX[3] = { pMatrix[0], pMatrix[1], pMatrix[2] };
		const float fAxisY[3] = { pMatrix[4], pMatrix[5], pMatrix[6] };
		const float fAxisZ[3] = { pMatrix[8], pMatrix[9], pMatrix[10] };
		const float fTranslation[3] = { pMatrix[12], pMatrix[13], pMatrix[14] };

		const float fScaleXSquared = (fAxisX[0] * fAxisX[0]) + (fAxisX[1] * fAxisX[1]) + (fAxisX[2] * fAxisX[2]);
		const float fScaleYSquared = (fAxisY[0] * fAxisY[0]) + (fAxisY[1] * fAxisY[1]) + (fAxisY[2] * fAxisY[2]);
		const float fScaleZSquared = (fAxisZ[0] * fAxisZ[0]) + (fAxisZ[1] * fAxisZ[1]) + (fAxisZ[2] * fAxisZ[2]);

		const float fInverseScaleX = (fScaleXSquared > 1e-12f) ? (1.0f / fScaleXSquared) : 0.0f;
		const float fInverseScaleY = (fScaleYSquared > 1e-12f) ? (1.0f / fScaleYSquared) : 0.0f;
		const float fInverseScaleZ = (fScaleZSquared > 1e-12f) ? (1.0f / fScaleZSquared) : 0.0f;

		// Rows of the inverse rotation are the original columns, normalized.
		const float fInverse00 = fAxisX[0] * fInverseScaleX;
		const float fInverse01 = fAxisY[0] * fInverseScaleY;
		const float fInverse02 = fAxisZ[0] * fInverseScaleZ;
		const float fInverse10 = fAxisX[1] * fInverseScaleX;
		const float fInverse11 = fAxisY[1] * fInverseScaleY;
		const float fInverse12 = fAxisZ[1] * fInverseScaleZ;
		const float fInverse20 = fAxisX[2] * fInverseScaleX;
		const float fInverse21 = fAxisY[2] * fInverseScaleY;
		const float fInverse22 = fAxisZ[2] * fInverseScaleZ;

		pOutMatrix[0] = fInverse00;
		pOutMatrix[1] = fInverse01;
		pOutMatrix[2] = fInverse02;
		pOutMatrix[3] = 0.0f;

		pOutMatrix[4] = fInverse10;
		pOutMatrix[5] = fInverse11;
		pOutMatrix[6] = fInverse12;
		pOutMatrix[7] = 0.0f;

		pOutMatrix[8] = fInverse20;
		pOutMatrix[9] = fInverse21;
		pOutMatrix[10] = fInverse22;
		pOutMatrix[11] = 0.0f;

		// The inverse translation is the world position taken into local space
		// with the sign flipped.
		pOutMatrix[12] = -((fInverse00 * fTranslation[0]) + (fInverse10 * fTranslation[1]) + (fInverse20 * fTranslation[2]));
		pOutMatrix[13] = -((fInverse01 * fTranslation[0]) + (fInverse11 * fTranslation[1]) + (fInverse21 * fTranslation[2]));
		pOutMatrix[14] = -((fInverse02 * fTranslation[0]) + (fInverse12 * fTranslation[1]) + (fInverse22 * fTranslation[2]));
		pOutMatrix[15] = 1.0f;
	}

	void Transform::GetWorldPosition(float& outPositionX, float& outPositionY, float& outPositionZ)
	{
		EnsureWorldMatrix();
		outPositionX = m_WorldMatrix[12];
		outPositionY = m_WorldMatrix[13];
		outPositionZ = m_WorldMatrix[14];
	}

	void Transform::GetWorldScale(float& outScaleX, float& outScaleY, float& outScaleZ)
	{
		EnsureWorldMatrix();

		// The length of each basis axis is the scale the chain applied to it.
		const float fAxisX[3] = { m_WorldMatrix[0], m_WorldMatrix[1], m_WorldMatrix[2] };
		const float fAxisY[3] = { m_WorldMatrix[4], m_WorldMatrix[5], m_WorldMatrix[6] };
		const float fAxisZ[3] = { m_WorldMatrix[8], m_WorldMatrix[9], m_WorldMatrix[10] };

		outScaleX = sqrtf((fAxisX[0] * fAxisX[0]) + (fAxisX[1] * fAxisX[1]) + (fAxisX[2] * fAxisX[2]));
		outScaleY = sqrtf((fAxisY[0] * fAxisY[0]) + (fAxisY[1] * fAxisY[1]) + (fAxisY[2] * fAxisY[2]));
		outScaleZ = sqrtf((fAxisZ[0] * fAxisZ[0]) + (fAxisZ[1] * fAxisZ[1]) + (fAxisZ[2] * fAxisZ[2]));
	}

	void Transform::GetWorldRotation(float& outRotationX, float& outRotationY, float& outRotationZ)
	{
		EnsureWorldMatrix();

		// The rotation this engine builds is always Rx * Ry * Rz, so the angles
		// can be read straight back out of the matrix. The axes are normalized
		// first, because the same matrix carries the chain's scale.
		const float fAxisLengthX = sqrtf((m_WorldMatrix[0] * m_WorldMatrix[0]) + (m_WorldMatrix[1] * m_WorldMatrix[1]) + (m_WorldMatrix[2] * m_WorldMatrix[2]));
		const float fAxisLengthY = sqrtf((m_WorldMatrix[4] * m_WorldMatrix[4]) + (m_WorldMatrix[5] * m_WorldMatrix[5]) + (m_WorldMatrix[6] * m_WorldMatrix[6]));
		const float fAxisLengthZ = sqrtf((m_WorldMatrix[8] * m_WorldMatrix[8]) + (m_WorldMatrix[9] * m_WorldMatrix[9]) + (m_WorldMatrix[10] * m_WorldMatrix[10]));
		const float fSafeLengthX = (fAxisLengthX > 1e-6f) ? fAxisLengthX : 1.0f;
		const float fSafeLengthY = (fAxisLengthY > 1e-6f) ? fAxisLengthY : 1.0f;
		const float fSafeLengthZ = (fAxisLengthZ > 1e-6f) ? fAxisLengthZ : 1.0f;

		// Row 0, 1 and 2 of the rotation, element (row, column) at column * 4 + row.
		const float fM00 = m_WorldMatrix[0] / fSafeLengthX;
		const float fM01 = m_WorldMatrix[4] / fSafeLengthY;
		const float fM02 = m_WorldMatrix[8] / fSafeLengthZ;
		const float fM10 = m_WorldMatrix[1] / fSafeLengthX;
		const float fM11 = m_WorldMatrix[5] / fSafeLengthY;
		const float fM12 = m_WorldMatrix[9] / fSafeLengthZ;
		const float fM22 = m_WorldMatrix[10] / fSafeLengthZ;

		const float fRadiansToDegrees = 57.29577951308232087680f;

		outRotationY = asinf((fM02 < -1.0f) ? -1.0f : ((fM02 > 1.0f) ? 1.0f : fM02));
		if (fabsf(fM02) < 0.9999f)
		{
			outRotationX = atan2f(-fM12, fM22);
			outRotationZ = atan2f(-fM01, fM00);
		}
		else
		{
			// Looking straight up or down: X and Z turn about the same axis, so
			// the whole turn is reported as X.
			outRotationX = atan2f((fM02 > 0.0f) ? fM10 : -fM10, fM11);
			outRotationZ = 0.0f;
		}

		outRotationX *= fRadiansToDegrees;
		outRotationY *= fRadiansToDegrees;
		outRotationZ *= fRadiansToDegrees;
	}

	void Transform::SetWorldPosition(float fPositionX, float fPositionY, float fPositionZ)
	{
		// Applying the PARENT's world-to-local matrix to a world position gives
		// the local position that lands there.
		float parentWorldToLocal[16] = {};
		Transform* pParent = ResolveParent();
		if (pParent != nullptr)
		{
			pParent->GetWorldToLocalMatrix(parentWorldToLocal);
		}
		else
		{
			BuildIdentityMatrix(parentWorldToLocal);
		}

		const float fLocalPositionX =
			(parentWorldToLocal[0] * fPositionX) + (parentWorldToLocal[4] * fPositionY) + (parentWorldToLocal[8] * fPositionZ) + parentWorldToLocal[12];
		const float fLocalPositionY =
			(parentWorldToLocal[1] * fPositionX) + (parentWorldToLocal[5] * fPositionY) + (parentWorldToLocal[9] * fPositionZ) + parentWorldToLocal[13];
		const float fLocalPositionZ =
			(parentWorldToLocal[2] * fPositionX) + (parentWorldToLocal[6] * fPositionY) + (parentWorldToLocal[10] * fPositionZ) + parentWorldToLocal[14];

		SetLocalPosition(fLocalPositionX, fLocalPositionY, fLocalPositionZ);
	}

	// -------------------------------------------------------------------------
	// Hierarchy
	// -------------------------------------------------------------------------

	NativeObjectHandle Transform::GetChildTransformHandle(uint32 uChildIndex) const
	{
		return (uChildIndex < m_ChildTransformHandles.GetSize())
			? m_ChildTransformHandles[uChildIndex]
			: k_nInvalidObjectHandle;
	}

	bool Transform::SetParentTransformHandle(NativeObjectHandle uParentTransformHandle)
	{
		if (uParentTransformHandle == m_uParentTransformHandle)
		{
			return true;
		}

		Scene& scene = Scene::Get();
		if (uParentTransformHandle != k_nInvalidObjectHandle)
		{
			Transform* pNewParent = scene.FindTransform(uParentTransformHandle);
			if (pNewParent == nullptr)
			{
				return false;
			}

			// A transform may not become its own ancestor: walk up from the new
			// parent and refuse when this transform comes by.
			const Transform* pAncestor = pNewParent;
			for (uint32 uGuard = 0; pAncestor != nullptr && uGuard < 1024; ++uGuard)
			{
				if (pAncestor == this)
				{
					return false;
				}

				const NativeObjectHandle uNextHandle = pAncestor->GetParentTransformHandle();
				pAncestor = (uNextHandle != k_nInvalidObjectHandle) ? scene.FindTransform(uNextHandle) : nullptr;
			}
		}

		DetachFromParent();

		m_uParentTransformHandle = uParentTransformHandle;
		if (uParentTransformHandle != k_nInvalidObjectHandle)
		{
			Transform* pNewParent = scene.FindTransform(uParentTransformHandle);
			if (pNewParent != nullptr)
			{
				pNewParent->m_ChildTransformHandles.Add(GetHandle());
			}
		}

		MarkWorldDirty();
		return true;
	}

	void Transform::DetachFromParent()
	{
		if (m_uParentTransformHandle == k_nInvalidObjectHandle)
		{
			return;
		}

		Transform* pParent = Scene::Get().FindTransform(m_uParentTransformHandle);
		if (pParent != nullptr)
		{
			for (size_t nChildIndex = 0; nChildIndex < pParent->m_ChildTransformHandles.GetSize(); ++nChildIndex)
			{
				if (pParent->m_ChildTransformHandles[nChildIndex] == GetHandle())
				{
					pParent->m_ChildTransformHandles.RemoveAt(nChildIndex);
					break;
				}
			}
		}

		m_uParentTransformHandle = k_nInvalidObjectHandle;
		MarkWorldDirty();
	}
}

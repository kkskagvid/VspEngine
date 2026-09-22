#include "RuntimePCH.h"

#include <cmath>
#include <cstring>

#include "Classes/Camera.h"
#include "Classes/Scene.h"
#include "Classes/Transform.h"
#include "Graphics/GraphicsSystem.h"

namespace Vsp
{
	namespace
	{
		constexpr float k_fPi = 3.14159265358979323846f;
		constexpr float k_fDegreesToRadians = k_fPi / 180.0f;
		constexpr float k_fRadiansToDegrees = 180.0f / k_fPi;

		// A minimum the clip planes and the sensor are held to, so a projection
		// never divides by zero.
		constexpr float k_fMinimumClipPlaneDistance = 0.0001f;

		void SetIdentityMatrix(float* pOutMatrix)
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
						fValue += pLeftMatrix[(uIndex * 4) + uRow] * pRightMatrix[(uColumn * 4) + uIndex];
					}
					result[(uColumn * 4) + uRow] = fValue;
				}
			}
			memcpy(pOutMatrix, result, sizeof(result));
		}
	}

	void Camera::SetOwnerGameObjectHandle(NativeObjectHandle uGameObjectHandle)
	{
		m_uOwnerGameObjectHandle = uGameObjectHandle;
	}

	NativeObjectHandle Camera::GetTransformHandle() const
	{
		return Scene::Get().FindGameObjectTransformHandle(m_uOwnerGameObjectHandle);
	}

	void Camera::SetProjectionMode(CameraProjectionMode eProjectionMode)
	{
		m_eProjectionMode = eProjectionMode;
	}

	void Camera::SetFieldOfView(float fFieldOfView)
	{
		// A projection needs an angle strictly inside (0, 180).
		if (fFieldOfView < 0.1f) { fFieldOfView = 0.1f; }
		if (fFieldOfView > 179.0f) { fFieldOfView = 179.0f; }
		m_fFieldOfView = fFieldOfView;
	}

	void Camera::SetOrthographicSize(float fOrthographicSize)
	{
		m_fOrthographicSize = (fOrthographicSize > k_fMinimumClipPlaneDistance)
			? fOrthographicSize
			: k_fMinimumClipPlaneDistance;
	}

	void Camera::SetNearClipPlane(float fNearClipPlane)
	{
		m_fNearClipPlane = (fNearClipPlane > k_fMinimumClipPlaneDistance)
			? fNearClipPlane
			: k_fMinimumClipPlaneDistance;
	}

	void Camera::SetFarClipPlane(float fFarClipPlane)
	{
		m_fFarClipPlane = (fFarClipPlane > m_fNearClipPlane) ? fFarClipPlane : (m_fNearClipPlane + 1.0f);
	}

	void Camera::SetFocalLength(float fFocalLength)
	{
		m_fFocalLength = (fFocalLength > k_fMinimumClipPlaneDistance) ? fFocalLength : k_fMinimumClipPlaneDistance;
	}

	void Camera::SetSensorWidth(float fSensorWidth)
	{
		m_fSensorWidth = (fSensorWidth > k_fMinimumClipPlaneDistance) ? fSensorWidth : k_fMinimumClipPlaneDistance;
	}

	void Camera::SetSensorHeight(float fSensorHeight)
	{
		m_fSensorHeight = (fSensorHeight > k_fMinimumClipPlaneDistance) ? fSensorHeight : k_fMinimumClipPlaneDistance;
	}

	void Camera::SetAperture(float fAperture)
	{
		m_fAperture = (fAperture > 0.0f) ? fAperture : 0.0f;
	}

	void Camera::SetFocusDistance(float fFocusDistance)
	{
		m_fFocusDistance = (fFocusDistance > 0.0f) ? fFocusDistance : 0.0f;
	}

	// -------------------------------------------------------------------------
	// What the settings work out to
	// -------------------------------------------------------------------------

	float Camera::GetEffectiveFieldOfView() const
	{
		if (m_eProjectionMode != CameraProjectionMode::Physical)
		{
			return m_fFieldOfView;
		}

		// A lens images the film back at f = focal length, so the half angle it
		// covers is atan(half the sensor / focal length).
		const float fHalfAngle = atanf((m_fSensorHeight * 0.5f) / m_fFocalLength);
		return 2.0f * fHalfAngle * k_fRadiansToDegrees;
	}

	float Camera::ResolveAspect() const
	{
		if (m_fAspect > 0.0f)
		{
			return m_fAspect;
		}

		// The projection needs the shape of what is being rendered into, which
		// only the graphics system knows.
		const uint32 uWidth = GraphicsSystem::Get().GetBackbufferWidth();
		const uint32 uHeight = GraphicsSystem::Get().GetBackbufferHeight();
		if (uWidth == 0 || uHeight == 0)
		{
			return 1.0f;
		}
		return static_cast<float>(uWidth) / static_cast<float>(uHeight);
	}

	// -------------------------------------------------------------------------
	// Matrices
	// -------------------------------------------------------------------------

	void Camera::GetViewMatrix(float* pOutMatrix16) const
	{
		if (pOutMatrix16 == nullptr)
		{
			return;
		}

		Transform* pTransform = Scene::Get().FindTransform(GetTransformHandle());
		if (pTransform == nullptr)
		{
			SetIdentityMatrix(pOutMatrix16);
			return;
		}

		// The view is the camera's world transform, inverted: the transform's
		// world-to-local matrix is exactly that.
		pTransform->GetWorldToLocalMatrix(pOutMatrix16);
	}

	void Camera::GetRotationOnlyViewMatrix(float* pOutMatrix16) const
	{
		if (pOutMatrix16 == nullptr)
		{
			return;
		}

		GetViewMatrix(pOutMatrix16);

		// Dropping the translation leaves the rotation (and the scale, which a
		// camera's transform does not use).
		pOutMatrix16[12] = 0.0f;
		pOutMatrix16[13] = 0.0f;
		pOutMatrix16[14] = 0.0f;
	}

	void Camera::GetProjectionMatrix(float* pOutMatrix16) const
	{
		if (pOutMatrix16 == nullptr)
		{
			return;
		}

		const float fAspect = ResolveAspect();
		const float fNear = m_fNearClipPlane;
		const float fFar = m_fFarClipPlane;

		if (m_eProjectionMode == CameraProjectionMode::Orthographic)
		{
			// A box of half-height "size": world units map to the screen
			// one-to-one however far away they are.
			const float fHalfHeight = m_fOrthographicSize;
			const float fHalfWidth = fHalfHeight * fAspect;

			for (uint32 uElementIndex = 0; uElementIndex < 16; ++uElementIndex)
			{
				pOutMatrix16[uElementIndex] = 0.0f;
			}

			pOutMatrix16[0] = 1.0f / fHalfWidth;
			// Vulkan clip space has Y pointing down, so the view's Y is flipped
			// here; world +Y then ends up at the top of the screen.
			pOutMatrix16[5] = -1.0f / fHalfHeight;
			pOutMatrix16[10] = 1.0f / (fNear - fFar);
			pOutMatrix16[14] = fNear / (fNear - fFar);
			pOutMatrix16[15] = 1.0f;
			return;
		}

		// Perspective, and the physical camera that derives its angle from a lens.
		const float fFieldOfView = GetEffectiveFieldOfView();
		const float fFocalScale = 1.0f / tanf(fFieldOfView * 0.5f * k_fDegreesToRadians);

		for (uint32 uElementIndex = 0; uElementIndex < 16; ++uElementIndex)
		{
			pOutMatrix16[uElementIndex] = 0.0f;
		}

		pOutMatrix16[0] = fFocalScale / fAspect;
		pOutMatrix16[5] = -fFocalScale;                     // Vulkan's Y flip.
		pOutMatrix16[10] = fFar / (fNear - fFar);
		pOutMatrix16[11] = -1.0f;
		pOutMatrix16[14] = (fNear * fFar) / (fNear - fFar);
	}

	void Camera::GetViewProjectionMatrix(float* pOutMatrix16) const
	{
		if (pOutMatrix16 == nullptr)
		{
			return;
		}

		float viewMatrix[16] = {};
		float projectionMatrix[16] = {};
		GetViewMatrix(viewMatrix);
		GetProjectionMatrix(projectionMatrix);
		MultiplyMatrices(projectionMatrix, viewMatrix, pOutMatrix16);
	}

	void Camera::GetWorldPosition(float& outPositionX, float& outPositionY, float& outPositionZ) const
	{
		Transform* pTransform = Scene::Get().FindTransform(GetTransformHandle());
		if (pTransform == nullptr)
		{
			outPositionX = 0.0f;
			outPositionY = 0.0f;
			outPositionZ = 0.0f;
			return;
		}
		pTransform->GetWorldPosition(outPositionX, outPositionY, outPositionZ);
	}

	bool Camera::IsPointVisible(float fWorldX, float fWorldY, float fWorldZ) const
	{
		// Everything in front of the camera (its local -Z) is potentially
		// visible; culling to the full frustum is a pipeline's business, so this
		// only answers the cheap question.
		float viewMatrix[16] = {};
		GetViewMatrix(viewMatrix);

		// Column-major: the view-space Z of a point is row 2 of the view.
		const float fViewSpaceZ =
			(viewMatrix[2] * fWorldX) + (viewMatrix[6] * fWorldY) + (viewMatrix[10] * fWorldZ) + viewMatrix[14];
		return fViewSpaceZ < 0.0f;
	}
}

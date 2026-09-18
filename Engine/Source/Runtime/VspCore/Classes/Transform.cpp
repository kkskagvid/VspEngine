#include "RuntimePCH.h"

#include "Classes/Transform.h"

namespace Vsp
{
	void Transform::SetOwnerGameObjectHandle(NativeObjectHandle uGameObjectHandle)
	{
		m_uOwnerGameObjectHandle = uGameObjectHandle;
	}

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
	}
}

#include "RuntimePCH.h"

#include "Classes/GameObject.h"

namespace Vsp
{
	void GameObject::AttachComponentHandle(NativeObjectHandle uComponentHandle)
	{
		if (uComponentHandle == k_nInvalidObjectHandle)
		{
			return;
		}

		for (size_t nComponentIndex = 0; nComponentIndex < m_ComponentHandles.GetSize(); ++nComponentIndex)
		{
			if (m_ComponentHandles[nComponentIndex] == uComponentHandle)
			{
				return;   // Already attached.
			}
		}
		m_ComponentHandles.Add(uComponentHandle);
	}

	bool GameObject::DetachComponentHandle(NativeObjectHandle uComponentHandle)
	{
		for (size_t nComponentIndex = 0; nComponentIndex < m_ComponentHandles.GetSize(); ++nComponentIndex)
		{
			if (m_ComponentHandles[nComponentIndex] == uComponentHandle)
			{
				m_ComponentHandles.RemoveAt(nComponentIndex);
				return true;
			}
		}
		return false;
	}

	void GameObject::SetTransformHandle(NativeObjectHandle uTransformHandle)
	{
		m_uTransformHandle = uTransformHandle;
	}

	void GameObject::ResetSceneLinks()
	{
		m_ComponentHandles.Clear();
		m_uTransformHandle = k_nInvalidObjectHandle;
	}
}

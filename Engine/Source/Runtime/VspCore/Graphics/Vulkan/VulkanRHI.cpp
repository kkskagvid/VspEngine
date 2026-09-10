#include "RuntimePCH.h"

#include "Core/Logging/Log.h"
#include "Graphics/Vulkan/VulkanRHI.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "VulkanContext";

	// -------------------------------------------------------------------------
	// VulkanContext: thin facade over the VulkanInstance and VulkanDevice
	// functional units. All real work lives in those units; this class only
	// wires the two together in the right order.
	// -------------------------------------------------------------------------

	bool VulkanContext::Initialize(const VspString& sApplicationName, void* pNativeWindowHandle)
	{
		if (IsInitialized())
		{
			LOG_ERROR(kLogTag, "VulkanContext is already initialized.");
			return false;
		}

		// 1. Instance + validation + window surface.
		if (!m_Instance.Initialize(sApplicationName, pNativeWindowHandle))
		{
			return false;
		}

		// 2. Physical device selection, logical device, queue, command pool.
		if (!m_Device.Initialize(m_Instance.GetInstance(), m_Instance.GetSurface()))
		{
			// Device negotiation failed (e.g. unsupported device): the device
			// unit already logged the details. Clean the instance back up.
			m_Instance.Destroy();
			return false;
		}

		LOG_INFO(kLogTag, "Vulkan context ready (feature path: Vulkan 1.3 bindless).");
		return true;
	}

	void VulkanContext::Destroy()
	{
		// Device before instance: the device borrows the instance's surface.
		m_Device.Destroy();
		m_Instance.Destroy();
	}
}

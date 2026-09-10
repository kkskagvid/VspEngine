#pragma once

#include <vulkan/vulkan.h>

#include "Core/Core.h"
#include "Core/String/VspString.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// VulkanInstance
	// -------------------------------------------------------------------------
	// Functional unit owning the Vulkan instance: loader-version check,
	// instance creation (application info, required surface extensions,
	// optional validation layer with a debug messenger) and the window
	// presentation surface. Split out of the former monolithic VulkanRHI so
	// instance concerns live in exactly one place.
	// Every function logs its own errors through the Log module and never
	// throws.
	// -------------------------------------------------------------------------
	class VulkanInstance
	{
	public:
		~VulkanInstance();

		// Creates the instance (plus the validation messenger when the layer
		// is present) and the window surface.
		bool Initialize(const VspString& sApplicationName, void* pNativeWindowHandle);

		// Destroys the surface, the debug messenger and the instance.
		void Destroy();

		bool IsInitialized() const { return m_VkInstance != VK_NULL_HANDLE; }

		VkInstance GetInstance() const { return m_VkInstance; }
		VkSurfaceKHR GetSurface() const { return m_VkSurface; }

	private:
		bool CreateInstance(const VspString& sApplicationName, bool& outValidationEnabled);
		bool CreateDebugMessenger();
		bool CreateSurface(void* pNativeWindowHandle);

		VkInstance m_VkInstance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_VkDebugMessenger = VK_NULL_HANDLE;
		VkSurfaceKHR m_VkSurface = VK_NULL_HANDLE;
	};
}

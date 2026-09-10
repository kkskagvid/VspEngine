#include "RuntimePCH.h"

#include <vector>

#include "Core/Logging/Log.h"
#include "Graphics/Vulkan/VulkanDevice.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "VulkanDevice";

	// -------------------------------------------------------------------------
	// Lifecycle
	// -------------------------------------------------------------------------

	VulkanDevice::~VulkanDevice()
	{
		Destroy();
	}

	bool VulkanDevice::Initialize(VkInstance instance, VkSurfaceKHR surface)
	{
		if (IsInitialized())
		{
			LOG_ERROR(kLogTag, "VulkanDevice is already initialized.");
			return false;
		}

		m_VkInstance = instance;
		m_VkSurface = surface;

		if (!SelectPhysicalDevice()) return false;
		if (!CreateLogicalDevice()) return false;
		if (!CreateCommandPool()) return false;
		return true;
	}

	void VulkanDevice::Destroy()
	{
		if (m_VkCommandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(m_VkDevice, m_VkCommandPool, nullptr);
			m_VkCommandPool = VK_NULL_HANDLE;
		}

		if (m_VkDevice != VK_NULL_HANDLE)
		{
			vkDestroyDevice(m_VkDevice, nullptr);
			m_VkDevice = VK_NULL_HANDLE;
		}

		m_VkPhysicalDevice = VK_NULL_HANDLE;
		m_VkGraphicsQueue = VK_NULL_HANDLE;
		m_nGraphicsQueueFamilyIndex = 0;
		m_VkInstance = VK_NULL_HANDLE;
		m_VkSurface = VK_NULL_HANDLE;
		m_DeviceProperties = VulkanDeviceProperties();
	}

	void VulkanDevice::WaitIdle() const
	{
		if (m_VkDevice != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_VkDevice);
		}
	}

	// -------------------------------------------------------------------------
	// Physical device selection + feature path negotiation
	// -------------------------------------------------------------------------

	bool VulkanDevice::SelectPhysicalDevice()
	{
		uint32 nDeviceCount = 0;
		vkEnumeratePhysicalDevices(m_VkInstance, &nDeviceCount, nullptr);
		if (nDeviceCount == 0)
		{
			LOG_ERROR(kLogTag, "No Vulkan-capable device was found on this machine.");
			return false;
		}

		std::vector<VkPhysicalDevice> devices(nDeviceCount);
		vkEnumeratePhysicalDevices(m_VkInstance, &nDeviceCount, devices.data());

		// Pick the first device that has a graphics queue family presenting to
		// the surface; prefer discrete GPUs.
		for (VkPhysicalDevice device : devices)
		{
			uint32 nQueueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &nQueueFamilyCount, nullptr);
			std::vector<VkQueueFamilyProperties> queueFamilies(nQueueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(device, &nQueueFamilyCount, queueFamilies.data());

			for (uint32 nFamilyIndex = 0; nFamilyIndex < nQueueFamilyCount; ++nFamilyIndex)
			{
				if ((queueFamilies[nFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)
				{
					continue;
				}

				VkBool32 bSupportsPresent = VK_FALSE;
				vkGetPhysicalDeviceSurfaceSupportKHR(device, nFamilyIndex, m_VkSurface, &bSupportsPresent);
				if (!bSupportsPresent)
				{
					continue;
				}

				m_VkPhysicalDevice = device;
				m_nGraphicsQueueFamilyIndex = nFamilyIndex;
				break;
			}

			if (m_VkPhysicalDevice != VK_NULL_HANDLE)
			{
				break;
			}
		}

		if (m_VkPhysicalDevice == VK_NULL_HANDLE)
		{
			LOG_ERROR(kLogTag, "No Vulkan device with graphics + present support was found.");
			return false;
		}

		VkPhysicalDeviceProperties deviceProperties = {};
		vkGetPhysicalDeviceProperties(m_VkPhysicalDevice, &deviceProperties);

		m_DeviceProperties.uApiVariant = VK_API_VERSION_VARIANT(deviceProperties.apiVersion);
		m_DeviceProperties.uApiMajor = VK_API_VERSION_MAJOR(deviceProperties.apiVersion);
		m_DeviceProperties.uApiMinor = VK_API_VERSION_MINOR(deviceProperties.apiVersion);
		m_DeviceProperties.uApiPatch = VK_API_VERSION_PATCH(deviceProperties.apiVersion);
		m_DeviceProperties.uVendorId = deviceProperties.vendorID;
		m_DeviceProperties.uDeviceId = deviceProperties.deviceID;
		m_DeviceProperties.sDeviceName = VspString(deviceProperties.deviceName);

		// ---- Bindless negotiation: Vulkan 1.3 + descriptor indexing ----
		// This engine has no Vulkan 1.2 fallback path, so any device that is
		// below 1.3 or lacks the bindless features is unsupported.
		VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {};
		indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;

		VkPhysicalDeviceFeatures2 features2 = {};
		features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
		features2.pNext = &indexingFeatures;
		vkGetPhysicalDeviceFeatures2(m_VkPhysicalDevice, &features2);

		const bool bIsVulkan13 = deviceProperties.apiVersion >= VK_API_VERSION_1_3;
		const bool bHasBindlessFeatures =
			indexingFeatures.descriptorBindingPartiallyBound == VK_TRUE &&
			indexingFeatures.runtimeDescriptorArray == VK_TRUE &&
			indexingFeatures.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;

		if (!bIsVulkan13)
		{
			m_DeviceProperties.eFeaturePath = VulkanFeaturePath::Unsupported;
			LOG_ERROR(kLogTag,
				"Unsupported device: this engine requires Vulkan 1.3 or higher (bindless only). Device '{}' reports Vulkan {}.{}.",
				m_DeviceProperties.sDeviceName,
				m_DeviceProperties.uApiMajor,
				m_DeviceProperties.uApiMinor);
			return false;
		}

		if (!bHasBindlessFeatures)
		{
			m_DeviceProperties.eFeaturePath = VulkanFeaturePath::Unsupported;
			LOG_ERROR(kLogTag,
				"Unsupported device: device '{}' is Vulkan 1.3 but lacks the bindless descriptor-indexing features (descriptorBindingPartiallyBound / runtimeDescriptorArray / shaderSampledImageArrayNonUniformIndexing).",
				m_DeviceProperties.sDeviceName);
			return false;
		}

		m_DeviceProperties.eFeaturePath = VulkanFeaturePath::Vulkan13Bindless;
		LOG_INFO(kLogTag, "Device '{}' (Vulkan {}.{}.{}) -> bindless path (Vulkan 1.3).",
			m_DeviceProperties.sDeviceName,
			m_DeviceProperties.uApiMajor, m_DeviceProperties.uApiMinor, m_DeviceProperties.uApiPatch);
		return true;
	}

	bool VulkanDevice::CreateLogicalDevice()
	{
		const float k_fQueuePriority = 1.0f;

		VkDeviceQueueCreateInfo queueCreateInfo = {};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = m_nGraphicsQueueFamilyIndex;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &k_fQueuePriority;

		VkPhysicalDeviceFeatures deviceFeatures = {};

		// Bindless is the only supported path: the descriptor-indexing features
		// it relies on are always enabled (SelectPhysicalDevice already
		// verified the device provides them).
		VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {};
		indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
		indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
		indexingFeatures.runtimeDescriptorArray = VK_TRUE;
		indexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;

		const char* k_sDeviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

		VkDeviceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		createInfo.queueCreateInfoCount = 1;
		createInfo.pQueueCreateInfos = &queueCreateInfo;
		createInfo.pEnabledFeatures = &deviceFeatures;
		createInfo.enabledExtensionCount = 1;
		createInfo.ppEnabledExtensionNames = k_sDeviceExtensions;
		createInfo.pNext = &indexingFeatures;

		const VkResult eResult = vkCreateDevice(m_VkPhysicalDevice, &createInfo, nullptr, &m_VkDevice);
		if (eResult != VK_SUCCESS || m_VkDevice == VK_NULL_HANDLE)
		{
			LOG_ERROR(kLogTag, "vkCreateDevice failed (VkResult {}).", static_cast<int32>(eResult));
			return false;
		}

		vkGetDeviceQueue(m_VkDevice, m_nGraphicsQueueFamilyIndex, 0, &m_VkGraphicsQueue);
		return true;
	}

	bool VulkanDevice::CreateCommandPool()
	{
		VkCommandPoolCreateInfo poolCreateInfo = {};
		poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolCreateInfo.queueFamilyIndex = m_nGraphicsQueueFamilyIndex;

		const VkResult eResult = vkCreateCommandPool(m_VkDevice, &poolCreateInfo, nullptr, &m_VkCommandPool);
		if (eResult != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreateCommandPool failed (VkResult {}).", static_cast<int32>(eResult));
			return false;
		}
		return true;
	}

	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	VkShaderModule VulkanDevice::CreateShaderModule(const uint32* pSpirvCode, size_t nByteCount) const
	{
		VkShaderModuleCreateInfo moduleCreateInfo = {};
		moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		moduleCreateInfo.codeSize = nByteCount;
		moduleCreateInfo.pCode = pSpirvCode;

		VkShaderModule shaderModule = VK_NULL_HANDLE;
		const VkResult eResult = vkCreateShaderModule(m_VkDevice, &moduleCreateInfo, nullptr, &shaderModule);
		if (eResult != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreateShaderModule failed (VkResult {}).", static_cast<int32>(eResult));
			return VK_NULL_HANDLE;
		}
		return shaderModule;
	}

	uint32 VulkanDevice::FindMemoryTypeIndex(uint32 uTypeFilter, VkMemoryPropertyFlags eProperties) const
	{
		VkPhysicalDeviceMemoryProperties memoryProperties = {};
		vkGetPhysicalDeviceMemoryProperties(m_VkPhysicalDevice, &memoryProperties);

		for (uint32 nTypeIndex = 0; nTypeIndex < memoryProperties.memoryTypeCount; ++nTypeIndex)
		{
			if ((uTypeFilter & (1u << nTypeIndex)) != 0 &&
				(memoryProperties.memoryTypes[nTypeIndex].propertyFlags & eProperties) == eProperties)
			{
				return nTypeIndex;
			}
		}
		return UINT32_MAX;
	}

	void VulkanDevice::AllocateBuffer(
		VkDeviceSize nByteSize,
		VkBufferUsageFlags eUsage,
		VkMemoryPropertyFlags eProperties,
		VkBuffer& outBuffer,
		VkDeviceMemory& outBufferMemory) const
	{
		outBuffer = VK_NULL_HANDLE;
		outBufferMemory = VK_NULL_HANDLE;

		VkBufferCreateInfo bufferCreateInfo = {};
		bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferCreateInfo.size = nByteSize;
		bufferCreateInfo.usage = eUsage;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		if (vkCreateBuffer(m_VkDevice, &bufferCreateInfo, nullptr, &outBuffer) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreateBuffer failed (size {} bytes).", static_cast<uint64_t>(nByteSize));
			return;
		}

		VkMemoryRequirements memoryRequirements = {};
		vkGetBufferMemoryRequirements(m_VkDevice, outBuffer, &memoryRequirements);

		const uint32 nMemoryTypeIndex = FindMemoryTypeIndex(memoryRequirements.memoryTypeBits, eProperties);
		if (nMemoryTypeIndex == UINT32_MAX)
		{
			LOG_ERROR(kLogTag, "No suitable memory type for buffer allocation.");
			return;
		}

		VkMemoryAllocateInfo allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = memoryRequirements.size;
		allocateInfo.memoryTypeIndex = nMemoryTypeIndex;

		if (vkAllocateMemory(m_VkDevice, &allocateInfo, nullptr, &outBufferMemory) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkAllocateMemory failed ({} bytes).", static_cast<uint64_t>(memoryRequirements.size));
			return;
		}

		vkBindBufferMemory(m_VkDevice, outBuffer, outBufferMemory, 0);
	}

	VkCommandBuffer VulkanDevice::BeginOneTimeCommandBuffer() const
	{
		VkCommandBufferAllocateInfo allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = m_VkCommandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		vkAllocateCommandBuffers(m_VkDevice, &allocateInfo, &commandBuffer);

		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkBeginCommandBuffer failed.");
			return VK_NULL_HANDLE;
		}
		return commandBuffer;
	}

	bool VulkanDevice::EndOneTimeCommandBuffer(VkCommandBuffer commandBuffer) const
	{
		vkEndCommandBuffer(commandBuffer);

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		const VkResult eSubmitResult = vkQueueSubmit(m_VkGraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
		vkQueueWaitIdle(m_VkGraphicsQueue);
		vkFreeCommandBuffers(m_VkDevice, m_VkCommandPool, 1, &commandBuffer);

		if (eSubmitResult != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "One-time command buffer submit failed (VkResult {}).", static_cast<int32>(eSubmitResult));
			return false;
		}
		return true;
	}
}

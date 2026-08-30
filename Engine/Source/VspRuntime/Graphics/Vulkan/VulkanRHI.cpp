#include "RuntimePCH.h"

#include <cstring>
#include <vector>

#include "Core/Logging/Log.h"
#include "Core/String/VspStringFormat.h"
#include "Graphics/Vulkan/VulkanRHI.h"

#if VSP_PLATFORM_WINDOWS
	#define WIN32_LEAN_AND_MEAN
	#include <Windows.h>
	#include <vulkan/vulkan_win32.h>
#endif

namespace Vsp
{
	static constexpr const char* kLogTag = "VulkanRHI";

	// Instance extensions required for presenting into a Win32 window.
	static const char* const k_sRequiredInstanceExtensions[] =
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	};
	static constexpr uint32_t k_nRequiredInstanceExtensionCount = 2;

	// Validation-layer messages are forwarded into the engine log.
	static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT eMessageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT eMessageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData)
	{
		LogLevel eLevel = LogLevel::Info;
		if ((eMessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
		{
			eLevel = LogLevel::Error;
		}
		else if ((eMessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0)
		{
			eLevel = LogLevel::Warning;
		}

		Log::Write(eLevel, "VulkanValidation",
			VspString(pCallbackData != nullptr && pCallbackData->pMessage != nullptr
				? pCallbackData->pMessage
				: "<no message>"));
		return VK_FALSE;
	}

	// -------------------------------------------------------------------------
	// Lifecycle
	// -------------------------------------------------------------------------

	VulkanContext::~VulkanContext()
	{
		Destroy();
	}

	bool VulkanContext::Initialize(const VspString& sApplicationName, void* pNativeWindowHandle)
	{
		if (IsInitialized())
		{
			LOG_ERROR(kLogTag, "VulkanContext is already initialized.");
			return false;
		}

		if (!CreateInstance(sApplicationName)) return false;
		if (!CreateSurface(pNativeWindowHandle)) return false;
		if (!SelectPhysicalDevice()) return false;
		if (!CreateLogicalDevice()) return false;
		if (!CreateCommandPool()) return false;
		return true;
	}

	void VulkanContext::Destroy()
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

		if (m_VkSurface != VK_NULL_HANDLE)
		{
			vkDestroySurfaceKHR(m_VkInstance, m_VkSurface, nullptr);
			m_VkSurface = VK_NULL_HANDLE;
		}

		if (m_VkDebugMessenger != VK_NULL_HANDLE)
		{
			auto pDestroyDebugMessengerFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
				vkGetInstanceProcAddr(m_VkInstance, "vkDestroyDebugUtilsMessengerEXT"));
			if (pDestroyDebugMessengerFn != nullptr)
			{
				pDestroyDebugMessengerFn(m_VkInstance, m_VkDebugMessenger, nullptr);
			}
			m_VkDebugMessenger = VK_NULL_HANDLE;
		}

		if (m_VkInstance != VK_NULL_HANDLE)
		{
			vkDestroyInstance(m_VkInstance, nullptr);
			m_VkInstance = VK_NULL_HANDLE;
		}
	}

	// -------------------------------------------------------------------------
	// Instance
	// -------------------------------------------------------------------------

	bool VulkanContext::CreateInstance(const VspString& sApplicationName)
	{
		// Ask the loader how high the instance API may go; clamp to 1.3.
		uint32_t nInstanceApiVersion = VK_API_VERSION_1_3;
		uint32_t nSupportedApiVersion = 0;
		if (vkEnumerateInstanceVersion(&nSupportedApiVersion) == VK_SUCCESS)
		{
			if (nSupportedApiVersion < VK_API_VERSION_1_3)
			{
				nInstanceApiVersion = nSupportedApiVersion;
			}
		}
		if (nInstanceApiVersion < VK_API_VERSION_1_2)
		{
			LOG_ERROR(kLogTag, "Unsupported Vulkan loader: version 1.2 or higher is required.");
			return false;
		}

		// Debug builds try to enable the validation layer when it is present
		// (set VSP_NO_VALIDATION=1 to skip it).
		std::vector<const char*> enabledLayers;
#if VSP_ENGINE_DEBUG
		{
			char sDisableBuffer[8] = {};
			const bool bValidationDisabled =
				GetEnvironmentVariableA("VSP_NO_VALIDATION", sDisableBuffer, sizeof(sDisableBuffer)) > 0;
			if (!bValidationDisabled)
			{
				uint32_t nLayerCount = 0;
				vkEnumerateInstanceLayerProperties(&nLayerCount, nullptr);
				std::vector<VkLayerProperties> availableLayers(nLayerCount);
				vkEnumerateInstanceLayerProperties(&nLayerCount, availableLayers.data());
				for (const VkLayerProperties& layer : availableLayers)
				{
					if (strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
					{
						enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
						LOG_INFO(kLogTag, "Vulkan validation layer enabled.");
						break;
					}
				}
			}
		}
#endif

		// The validation messenger needs VK_EXT_debug_utils on the instance.
		std::vector<const char*> enabledExtensions(
			k_sRequiredInstanceExtensions,
			k_sRequiredInstanceExtensions + k_nRequiredInstanceExtensionCount);
		if (!enabledLayers.empty())
		{
			enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		VkApplicationInfo applicationInfo = {};
		applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		applicationInfo.pApplicationName = sApplicationName.GetData();
		applicationInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
		applicationInfo.pEngineName = "VspRuntime";
		applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
		applicationInfo.apiVersion = nInstanceApiVersion;

		VkInstanceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &applicationInfo;
		createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
		createInfo.ppEnabledExtensionNames = enabledExtensions.data();
		createInfo.enabledLayerCount = static_cast<uint32_t>(enabledLayers.size());
		createInfo.ppEnabledLayerNames = enabledLayers.empty() ? nullptr : enabledLayers.data();

		const VkResult eResult = vkCreateInstance(&createInfo, nullptr, &m_VkInstance);
		if (eResult != VK_SUCCESS || m_VkInstance == VK_NULL_HANDLE)
		{
			LOG_ERROR(kLogTag, "vkCreateInstance failed (VkResult {}).", static_cast<int32_t>(eResult));
			return false;
		}

		// Forward validation-layer messages into the engine log.
		if (!enabledLayers.empty())
		{
			auto pCreateDebugMessengerFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
				vkGetInstanceProcAddr(m_VkInstance, "vkCreateDebugUtilsMessengerEXT"));
			if (pCreateDebugMessengerFn != nullptr)
			{
				VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo = {};
				messengerCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
				messengerCreateInfo.messageSeverity =
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
				messengerCreateInfo.messageType =
					VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
					VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
				messengerCreateInfo.pfnUserCallback = DebugMessengerCallback;
				messengerCreateInfo.pUserData = nullptr;
				pCreateDebugMessengerFn(m_VkInstance, &messengerCreateInfo, nullptr, &m_VkDebugMessenger);
			}
		}
		return true;
	}

	bool VulkanContext::CreateSurface(void* pNativeWindowHandle)
	{
#if VSP_PLATFORM_WINDOWS
		VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
		surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		surfaceCreateInfo.hinstance = GetModuleHandleW(nullptr);
		surfaceCreateInfo.hwnd = static_cast<HWND>(pNativeWindowHandle);

		const VkResult eResult = vkCreateWin32SurfaceKHR(m_VkInstance, &surfaceCreateInfo, nullptr, &m_VkSurface);
		if (eResult != VK_SUCCESS || m_VkSurface == VK_NULL_HANDLE)
		{
			LOG_ERROR(kLogTag, "vkCreateWin32SurfaceKHR failed (VkResult {}).", static_cast<int32_t>(eResult));
			return false;
		}
		return true;
#else
		LOG_ERROR(kLogTag, "Unsupported platform: no surface implementation.");
		return false;
#endif
	}

	// -------------------------------------------------------------------------
	// Physical device selection + feature path negotiation
	// -------------------------------------------------------------------------

	bool VulkanContext::SelectPhysicalDevice()
	{
		uint32_t nDeviceCount = 0;
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
			uint32_t nQueueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &nQueueFamilyCount, nullptr);
			std::vector<VkQueueFamilyProperties> queueFamilies(nQueueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(device, &nQueueFamilyCount, queueFamilies.data());

			for (uint32_t nFamilyIndex = 0; nFamilyIndex < nQueueFamilyCount; ++nFamilyIndex)
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

	bool VulkanContext::CreateLogicalDevice()
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
			LOG_ERROR(kLogTag, "vkCreateDevice failed (VkResult {}).", static_cast<int32_t>(eResult));
			return false;
		}

		vkGetDeviceQueue(m_VkDevice, m_nGraphicsQueueFamilyIndex, 0, &m_VkGraphicsQueue);
		return true;
	}

	bool VulkanContext::CreateCommandPool()
	{
		VkCommandPoolCreateInfo poolCreateInfo = {};
		poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolCreateInfo.queueFamilyIndex = m_nGraphicsQueueFamilyIndex;

		const VkResult eResult = vkCreateCommandPool(m_VkDevice, &poolCreateInfo, nullptr, &m_VkCommandPool);
		if (eResult != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreateCommandPool failed (VkResult {}).", static_cast<int32_t>(eResult));
			return false;
		}
		return true;
	}

	// -------------------------------------------------------------------------
	// Helpers
	// -------------------------------------------------------------------------

	VkShaderModule VulkanContext::CreateShaderModule(const uint32_t* pSpirvCode, size_t nByteCount) const
	{
		VkShaderModuleCreateInfo moduleCreateInfo = {};
		moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		moduleCreateInfo.codeSize = nByteCount;
		moduleCreateInfo.pCode = pSpirvCode;

		VkShaderModule shaderModule = VK_NULL_HANDLE;
		const VkResult eResult = vkCreateShaderModule(m_VkDevice, &moduleCreateInfo, nullptr, &shaderModule);
		if (eResult != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreateShaderModule failed (VkResult {}).", static_cast<int32_t>(eResult));
			return VK_NULL_HANDLE;
		}
		return shaderModule;
	}

	uint32_t VulkanContext::FindMemoryTypeIndex(uint32_t uTypeFilter, VkMemoryPropertyFlags eProperties) const
	{
		VkPhysicalDeviceMemoryProperties memoryProperties = {};
		vkGetPhysicalDeviceMemoryProperties(m_VkPhysicalDevice, &memoryProperties);

		for (uint32_t nTypeIndex = 0; nTypeIndex < memoryProperties.memoryTypeCount; ++nTypeIndex)
		{
			if ((uTypeFilter & (1u << nTypeIndex)) != 0 &&
				(memoryProperties.memoryTypes[nTypeIndex].propertyFlags & eProperties) == eProperties)
			{
				return nTypeIndex;
			}
		}
		return UINT32_MAX;
	}

	void VulkanContext::AllocateBuffer(
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

		const uint32_t nMemoryTypeIndex = FindMemoryTypeIndex(memoryRequirements.memoryTypeBits, eProperties);
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

	VkCommandBuffer VulkanContext::BeginOneTimeCommandBuffer() const
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

	bool VulkanContext::EndOneTimeCommandBuffer(VkCommandBuffer commandBuffer) const
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
			LOG_ERROR(kLogTag, "One-time command buffer submit failed (VkResult {}).", static_cast<int32_t>(eSubmitResult));
			return false;
		}
		return true;
	}
}

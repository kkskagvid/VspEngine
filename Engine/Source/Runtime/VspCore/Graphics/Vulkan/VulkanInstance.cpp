#include "RuntimePCH.h"

#include <cstring>
#include <vector>

#include "Common/PlatformMisc.h"
#include "Core/Logging/Log.h"
#include "Graphics/Vulkan/VulkanInstance.h"

// The Win32 surface implementation is Windows-only by nature; it stays
// behind #if VSP_PLATFORM_WINDOWS. Process-environment access goes through
// PlatformMisc (Common).
#if VSP_PLATFORM_WINDOWS
	#define WIN32_LEAN_AND_MEAN
	#include <Windows.h>
	#include <vulkan/vulkan_win32.h>
#endif

namespace Vsp
{
	static constexpr const char* kLogTag = "VulkanInstance";

	// Instance extensions required for presenting into a Win32 window.
	static const char* const k_sRequiredInstanceExtensions[] =
	{
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	};
	static constexpr uint32 k_nRequiredInstanceExtensionCount = 2;

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

	VulkanInstance::~VulkanInstance()
	{
		Destroy();
	}

	bool VulkanInstance::Initialize(const VspString& sApplicationName, void* pNativeWindowHandle)
	{
		if (IsInitialized())
		{
			LOG_ERROR(kLogTag, "VulkanInstance is already initialized.");
			return false;
		}

		bool bValidationEnabled = false;
		if (!CreateInstance(sApplicationName, bValidationEnabled)) return false;
		if (bValidationEnabled && !CreateDebugMessenger()) return false;
		if (!CreateSurface(pNativeWindowHandle)) return false;
		return true;
	}

	void VulkanInstance::Destroy()
	{
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
	// Instance creation
	// -------------------------------------------------------------------------

	bool VulkanInstance::CreateInstance(const VspString& sApplicationName, bool& outValidationEnabled)
	{
		outValidationEnabled = false;

		// Ask the loader how high the instance API may go; clamp to 1.3.
		uint32 nInstanceApiVersion = VK_API_VERSION_1_3;
		uint32 nSupportedApiVersion = 0;
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
				PlatformMisc::GetEnvironmentVariableValue("VSP_NO_VALIDATION", sDisableBuffer, sizeof(sDisableBuffer)) > 0;
			if (!bValidationDisabled)
			{
				uint32 nLayerCount = 0;
				vkEnumerateInstanceLayerProperties(&nLayerCount, nullptr);
				std::vector<VkLayerProperties> availableLayers(nLayerCount);
				vkEnumerateInstanceLayerProperties(&nLayerCount, availableLayers.data());
				for (const VkLayerProperties& layer : availableLayers)
				{
					if (strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
					{
						enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
						outValidationEnabled = true;
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
		applicationInfo.pEngineName = "VspCore";
		applicationInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
		applicationInfo.apiVersion = nInstanceApiVersion;

		VkInstanceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &applicationInfo;
		createInfo.enabledExtensionCount = static_cast<uint32>(enabledExtensions.size());
		createInfo.ppEnabledExtensionNames = enabledExtensions.data();
		createInfo.enabledLayerCount = static_cast<uint32>(enabledLayers.size());
		createInfo.ppEnabledLayerNames = enabledLayers.empty() ? nullptr : enabledLayers.data();

		const VkResult eResult = vkCreateInstance(&createInfo, nullptr, &m_VkInstance);
		if (eResult != VK_SUCCESS || m_VkInstance == VK_NULL_HANDLE)
		{
			LOG_ERROR(kLogTag, "vkCreateInstance failed (VkResult {}).", static_cast<int32>(eResult));
			return false;
		}
		return true;
	}

	bool VulkanInstance::CreateDebugMessenger()
	{
		// Forward validation-layer messages into the engine log.
		auto pCreateDebugMessengerFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(m_VkInstance, "vkCreateDebugUtilsMessengerEXT"));
		if (pCreateDebugMessengerFn == nullptr)
		{
			return true;   // No messenger entry point: validation still works without callbacks.
		}

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

		const VkResult eResult = pCreateDebugMessengerFn(
			m_VkInstance, &messengerCreateInfo, nullptr, &m_VkDebugMessenger);
		if (eResult != VK_SUCCESS)
		{
			LOG_WARNING(kLogTag, "vkCreateDebugUtilsMessengerEXT failed (VkResult {}).", static_cast<int32>(eResult));
		}
		return true;
	}

	// -------------------------------------------------------------------------
	// Surface creation
	// -------------------------------------------------------------------------

	bool VulkanInstance::CreateSurface(void* pNativeWindowHandle)
	{
#if VSP_PLATFORM_WINDOWS
		VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
		surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		surfaceCreateInfo.hinstance = static_cast<HINSTANCE>(PlatformMisc::GetProcessModuleHandle());
		surfaceCreateInfo.hwnd = static_cast<HWND>(pNativeWindowHandle);

		const VkResult eResult = vkCreateWin32SurfaceKHR(m_VkInstance, &surfaceCreateInfo, nullptr, &m_VkSurface);
		if (eResult != VK_SUCCESS || m_VkSurface == VK_NULL_HANDLE)
		{
			LOG_ERROR(kLogTag, "vkCreateWin32SurfaceKHR failed (VkResult {}).", static_cast<int32>(eResult));
			return false;
		}
		return true;
#else
		LOG_ERROR(kLogTag, "Unsupported platform: no surface implementation.");
		return false;
#endif
	}
}

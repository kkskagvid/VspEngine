#pragma once

#include <vulkan/vulkan.h>

#include "Core/Core.h"
#include "Core/String/VspString.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// VulkanFeaturePath
	// -------------------------------------------------------------------------
	// Which rendering implementation the device runs:
	//   Unsupported      - device reports Vulkan < 1.2 (engine reports an error)
	//   Vulkan12Fallback - classic descriptor sets (Vulkan 1.2 device, or a
	//                      1.3 device without the bindless feature bits)
	//   Vulkan13Bindless - Vulkan 1.3 with descriptor indexing: large sampled
	//                      image arrays indexed with nonuniformEXT
	// -------------------------------------------------------------------------
	enum class VulkanFeaturePath : uint32_t
	{
		Unsupported = 0,
		Vulkan12Fallback = 1,
		Vulkan13Bindless = 2,
	};

	struct VulkanDeviceProperties
	{
		uint32_t uApiVariant = 0;
		uint32_t uApiMajor = 0;
		uint32_t uApiMinor = 0;
		uint32_t uApiPatch = 0;
		uint32_t uVendorId = 0;
		uint32_t uDeviceId = 0;
		VulkanFeaturePath eFeaturePath = VulkanFeaturePath::Unsupported;
		VspString sDeviceName;
	};

	// -------------------------------------------------------------------------
	// VulkanContext
	// -------------------------------------------------------------------------
	// Owns the Vulkan instance, the selected physical device, the logical
	// device, the graphics queue and the presentation surface.
	// Device negotiation happens inside Initialize():
	//   - device apiVersion < 1.2   -> failure with an "unsupported device"
	//                                  error message (never throws)
	//   - device apiVersion >= 1.3 with descriptor-indexing features
	//                                -> Vulkan13Bindless path
	//   - otherwise (1.2 <= api < 1.3, or 1.3 without bindless features)
	//                                -> Vulkan12Fallback path
	// -------------------------------------------------------------------------
	class VulkanContext
	{
	public:
		~VulkanContext();

		// Creates instance + surface, picks the device and negotiates the path.
		bool Initialize(const VspString& sApplicationName, void* pNativeWindowHandle, VspString& outErrorText);

		void Destroy();

		bool IsInitialized() const { return m_VkDevice != VK_NULL_HANDLE; }

		VkInstance GetInstance() const { return m_VkInstance; }
		VkPhysicalDevice GetPhysicalDevice() const { return m_VkPhysicalDevice; }
		VkDevice GetDevice() const { return m_VkDevice; }
		VkQueue GetGraphicsQueue() const { return m_VkGraphicsQueue; }
		uint32_t GetGraphicsQueueFamilyIndex() const { return m_nGraphicsQueueFamilyIndex; }
		VkCommandPool GetCommandPool() const { return m_VkCommandPool; }
		VkSurfaceKHR GetSurface() const { return m_VkSurface; }
		const VulkanDeviceProperties& GetDeviceProperties() const { return m_DeviceProperties; }
		bool SupportsBindless() const { return m_DeviceProperties.eFeaturePath == VulkanFeaturePath::Vulkan13Bindless; }

		// -------- Small helpers shared by the renderer -----------------------
		VkShaderModule CreateShaderModule(const uint32_t* pSpirvCode, size_t nByteCount, VspString& outErrorText) const;
		uint32_t FindMemoryTypeIndex(uint32_t uTypeFilter, VkMemoryPropertyFlags eProperties) const;
		void AllocateBuffer(
			VkDeviceSize nByteSize,
			VkBufferUsageFlags eUsage,
			VkMemoryPropertyFlags eProperties,
			VkBuffer& outBuffer,
			VkDeviceMemory& outBufferMemory,
			VspString& outErrorText) const;

		// One-shot command buffer for upload work.
		VkCommandBuffer BeginOneTimeCommandBuffer(VspString& outErrorText) const;
		bool EndOneTimeCommandBuffer(VkCommandBuffer commandBuffer, VspString& outErrorText) const;

	private:
		bool CreateInstance(const VspString& sApplicationName, VspString& outErrorText);
		bool CreateSurface(void* pNativeWindowHandle, VspString& outErrorText);
		bool SelectPhysicalDevice(VspString& outErrorText);
		bool CreateLogicalDevice(VspString& outErrorText);
		bool CreateCommandPool(VspString& outErrorText);

		VkInstance m_VkInstance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_VkDebugMessenger = VK_NULL_HANDLE;
		VkSurfaceKHR m_VkSurface = VK_NULL_HANDLE;
		VkPhysicalDevice m_VkPhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_VkDevice = VK_NULL_HANDLE;
		VkQueue m_VkGraphicsQueue = VK_NULL_HANDLE;
		uint32_t m_nGraphicsQueueFamilyIndex = 0;
		VkCommandPool m_VkCommandPool = VK_NULL_HANDLE;
		VulkanDeviceProperties m_DeviceProperties;
	};
}

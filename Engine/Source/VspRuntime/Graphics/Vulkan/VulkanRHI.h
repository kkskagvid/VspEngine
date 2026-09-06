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
	//   Unsupported      - the device cannot run the engine (it reports
	//                      Vulkan < 1.3 or lacks bindless descriptor indexing)
	//   Vulkan13Bindless - Vulkan 1.3 with descriptor indexing: large sampled
	//                      image arrays indexed with nonuniformEXT
	// There is no Vulkan 1.2 fallback path: this engine only supports the
	// Vulkan 1.3 bindless implementation.
	// -------------------------------------------------------------------------
	enum class VulkanFeaturePath : uint32
	{
		Unsupported = 0,
		Vulkan13Bindless = 1,
	};

	struct VulkanDeviceProperties
	{
		uint32 uApiVariant = 0;
		uint32 uApiMajor = 0;
		uint32 uApiMinor = 0;
		uint32 uApiPatch = 0;
		uint32 uVendorId = 0;
		uint32 uDeviceId = 0;
		VulkanFeaturePath eFeaturePath = VulkanFeaturePath::Unsupported;
		VspString sDeviceName;
	};

	// -------------------------------------------------------------------------
	// VulkanContext
	// -------------------------------------------------------------------------
	// Owns the Vulkan instance, the selected physical device, the logical
	// device, the graphics queue and the presentation surface.
	// Device negotiation happens inside Initialize():
	//   - device apiVersion >= 1.3 with descriptor-indexing features
	//                                -> Vulkan13Bindless path
	//   - anything else (Vulkan < 1.3, or 1.3 without bindless features)
	//                                -> failure with an "unsupported device"
	//                                   error logged through the Log module
	// Every function logs its own errors and never throws.
	// -------------------------------------------------------------------------
	class VulkanContext
	{
	public:
		~VulkanContext();

		// Creates instance + surface, picks the device and negotiates the path.
		bool Initialize(const VspString& sApplicationName, void* pNativeWindowHandle);

		void Destroy();

		bool IsInitialized() const { return m_VkDevice != VK_NULL_HANDLE; }

		VkInstance GetInstance() const { return m_VkInstance; }
		VkPhysicalDevice GetPhysicalDevice() const { return m_VkPhysicalDevice; }
		VkDevice GetDevice() const { return m_VkDevice; }
		VkQueue GetGraphicsQueue() const { return m_VkGraphicsQueue; }
		uint32 GetGraphicsQueueFamilyIndex() const { return m_nGraphicsQueueFamilyIndex; }
		VkCommandPool GetCommandPool() const { return m_VkCommandPool; }
		VkSurfaceKHR GetSurface() const { return m_VkSurface; }
		const VulkanDeviceProperties& GetDeviceProperties() const { return m_DeviceProperties; }
		bool SupportsBindless() const { return m_DeviceProperties.eFeaturePath == VulkanFeaturePath::Vulkan13Bindless; }

		// -------- Small helpers shared by the renderer -----------------------
		VkShaderModule CreateShaderModule(const uint32* pSpirvCode, size_t nByteCount) const;
		uint32 FindMemoryTypeIndex(uint32 uTypeFilter, VkMemoryPropertyFlags eProperties) const;
		void AllocateBuffer(
			VkDeviceSize nByteSize,
			VkBufferUsageFlags eUsage,
			VkMemoryPropertyFlags eProperties,
			VkBuffer& outBuffer,
			VkDeviceMemory& outBufferMemory) const;

		// One-shot command buffer for upload work.
		VkCommandBuffer BeginOneTimeCommandBuffer() const;
		bool EndOneTimeCommandBuffer(VkCommandBuffer commandBuffer) const;

	private:
		bool CreateInstance(const VspString& sApplicationName);
		bool CreateSurface(void* pNativeWindowHandle);
		bool SelectPhysicalDevice();
		bool CreateLogicalDevice();
		bool CreateCommandPool();

		VkInstance m_VkInstance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT m_VkDebugMessenger = VK_NULL_HANDLE;
		VkSurfaceKHR m_VkSurface = VK_NULL_HANDLE;
		VkPhysicalDevice m_VkPhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_VkDevice = VK_NULL_HANDLE;
		VkQueue m_VkGraphicsQueue = VK_NULL_HANDLE;
		uint32 m_nGraphicsQueueFamilyIndex = 0;
		VkCommandPool m_VkCommandPool = VK_NULL_HANDLE;
		VulkanDeviceProperties m_DeviceProperties;
	};
}

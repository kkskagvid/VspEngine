#pragma once

#include <vulkan/vulkan.h>

#include "Core/Core.h"
#include "Core/String/VspString.h"
#include "Graphics/Vulkan/VulkanInstance.h"
#include "Graphics/Vulkan/VulkanDevice.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// VulkanContext
	// -------------------------------------------------------------------------
	// The RHI facade the renderer and the swapchain talk to. It is a thin
	// composition of the split functional units:
	//   - VulkanInstance: instance + validation messenger + window surface
	//   - VulkanDevice:    physical/logical device, queue, command pool and
	//                      the shared allocation/command helpers
	// Everything device-related (device properties, feature path, buffer and
	// one-time-command-buffer helpers) is forwarded into VulkanDevice; the
	// facade itself holds no Vulkan handles.
	// Every function logs its own errors and never throws.
	// -------------------------------------------------------------------------
	class VulkanContext
	{
	public:
		// Creates instance + surface, picks the device and negotiates the path.
		bool Initialize(const VspString& sApplicationName, void* pNativeWindowHandle);

		void Destroy();

		bool IsInitialized() const { return m_Device.IsInitialized(); }

		VkInstance GetInstance() const { return m_Instance.GetInstance(); }
		VkSurfaceKHR GetSurface() const { return m_Instance.GetSurface(); }
		VkPhysicalDevice GetPhysicalDevice() const { return m_Device.GetPhysicalDevice(); }
		VkDevice GetDevice() const { return m_Device.GetDevice(); }
		VkQueue GetGraphicsQueue() const { return m_Device.GetGraphicsQueue(); }
		uint32 GetGraphicsQueueFamilyIndex() const { return m_Device.GetGraphicsQueueFamilyIndex(); }
		VkCommandPool GetCommandPool() const { return m_Device.GetCommandPool(); }
		const VulkanDeviceProperties& GetDeviceProperties() const { return m_Device.GetDeviceProperties(); }
		bool SupportsBindless() const { return m_Device.SupportsBindless(); }

		// The device unit behind the facade (used by the resource units).
		const VulkanDevice& GetDeviceObject() const { return m_Device; }

		// -------- Small helpers shared by the higher-level units ------------
		VkShaderModule CreateShaderModule(const uint32* pSpirvCode, size_t nByteCount) const
		{
			return m_Device.CreateShaderModule(pSpirvCode, nByteCount);
		}

		uint32 FindMemoryTypeIndex(uint32 uTypeFilter, VkMemoryPropertyFlags eProperties) const
		{
			return m_Device.FindMemoryTypeIndex(uTypeFilter, eProperties);
		}

		void AllocateBuffer(
			VkDeviceSize nByteSize,
			VkBufferUsageFlags eUsage,
			VkMemoryPropertyFlags eProperties,
			VkBuffer& outBuffer,
			VkDeviceMemory& outBufferMemory) const
		{
			m_Device.AllocateBuffer(nByteSize, eUsage, eProperties, outBuffer, outBufferMemory);
		}

		// One-shot command buffer for upload work.
		VkCommandBuffer BeginOneTimeCommandBuffer() const
		{
			return m_Device.BeginOneTimeCommandBuffer();
		}

		bool EndOneTimeCommandBuffer(VkCommandBuffer commandBuffer) const
		{
			return m_Device.EndOneTimeCommandBuffer(commandBuffer);
		}

		void WaitIdle() const { m_Device.WaitIdle(); }

	private:
		VulkanInstance m_Instance;
		VulkanDevice m_Device;
	};
}

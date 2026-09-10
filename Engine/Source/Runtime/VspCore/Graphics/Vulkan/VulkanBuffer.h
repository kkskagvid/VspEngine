#pragma once

#include <vulkan/vulkan.h>

#include "Core/Core.h"

namespace Vsp
{
	class VulkanContext;

	// -------------------------------------------------------------------------
	// VulkanBuffer
	// -------------------------------------------------------------------------
	// Functional unit owning one VkBuffer plus its bound device memory.
	// Allocate() creates buffer + memory with the requested usage and memory
	// properties; WriteData() maps the memory, copies the bytes and unmaps
	// (the buffer must be host-visible). Destroy() releases everything.
	// Every function logs its own errors through the Log module and never
	// throws.
	// -------------------------------------------------------------------------
	class VulkanBuffer
	{
	public:
		~VulkanBuffer();

		// Creates the buffer and its device memory. Returns false on failure.
		bool Allocate(
			const VulkanContext& context,
			VkDeviceSize nByteSize,
			VkBufferUsageFlags eUsage,
			VkMemoryPropertyFlags eProperties);

		// Maps, copies nByteSize bytes and unmaps. Fails when the buffer is
		// not host-visible or nByteSize exceeds the allocated size.
		bool WriteData(const VulkanContext& context, const void* pData, VkDeviceSize nByteSize);

		void Destroy(const VulkanContext& context);

		bool IsValid() const { return m_VkBuffer != VK_NULL_HANDLE; }
		VkBuffer GetBuffer() const { return m_VkBuffer; }
		VkDeviceMemory GetDeviceMemory() const { return m_VkDeviceMemory; }
		VkDeviceSize GetByteSize() const { return m_nByteSize; }

	private:
		VkBuffer m_VkBuffer = VK_NULL_HANDLE;
		VkDeviceMemory m_VkDeviceMemory = VK_NULL_HANDLE;
		VkDeviceSize m_nByteSize = 0;
	};
}

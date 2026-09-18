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
		VulkanBuffer() = default;
		~VulkanBuffer();

		// A buffer owns its handles, so it moves but never copies; moving
		// leaves the source empty (Destroy() on it then does nothing).
		VulkanBuffer(const VulkanBuffer&) = delete;
		VulkanBuffer& operator=(const VulkanBuffer&) = delete;
		VulkanBuffer(VulkanBuffer&& Other) noexcept;
		VulkanBuffer& operator=(VulkanBuffer&& Other) noexcept;

		// Creates the buffer and its device memory. Returns false on failure.
		bool Allocate(
			const VulkanContext& context,
			VkDeviceSize nByteSize,
			VkBufferUsageFlags eUsage,
			VkMemoryPropertyFlags eProperties);

		// Maps, copies nByteCount bytes to nByteOffset and unmaps. Fails when
		// the buffer is not host-visible or the range exceeds the allocation.
		bool WriteData(
			const VulkanContext& context,
			VkDeviceSize nByteOffset,
			const void* pData,
			VkDeviceSize nByteCount);

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

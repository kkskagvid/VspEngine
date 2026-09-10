#include "RuntimePCH.h"

#include <cstring>

#include "Core/Logging/Log.h"
#include "Graphics/Vulkan/VulkanBuffer.h"
#include "Graphics/Vulkan/VulkanRHI.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "VulkanBuffer";

	VulkanBuffer::~VulkanBuffer()
	{
		// The context outlives the buffer in every use of this unit; if the
		// buffer is still alive past its context the handles are already
		// invalid and nothing can be destroyed here safely. Destroy() is the
		// explicit teardown path.
	}

	bool VulkanBuffer::Allocate(
		const VulkanContext& context,
		VkDeviceSize nByteSize,
		VkBufferUsageFlags eUsage,
		VkMemoryPropertyFlags eProperties)
	{
		if (IsValid())
		{
			LOG_ERROR(kLogTag, "VulkanBuffer is already allocated.");
			return false;
		}

		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory bufferMemory = VK_NULL_HANDLE;
		context.AllocateBuffer(nByteSize, eUsage, eProperties, buffer, bufferMemory);
		if (buffer == VK_NULL_HANDLE)
		{
			return false;
		}

		m_VkBuffer = buffer;
		m_VkDeviceMemory = bufferMemory;
		m_nByteSize = nByteSize;
		return true;
	}

	bool VulkanBuffer::WriteData(const VulkanContext& context, const void* pData, VkDeviceSize nByteSize)
	{
		if (!IsValid())
		{
			LOG_ERROR(kLogTag, "Cannot write to an unallocated buffer.");
			return false;
		}
		if (pData == nullptr || nByteSize == 0 || nByteSize > m_nByteSize)
		{
			LOG_ERROR(kLogTag, "Invalid data range for buffer write.");
			return false;
		}

		void* pMappedData = nullptr;
		if (vkMapMemory(context.GetDevice(), m_VkDeviceMemory, 0, nByteSize, 0, &pMappedData) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkMapMemory failed.");
			return false;
		}

		memcpy(pMappedData, pData, static_cast<size_t>(nByteSize));
		vkUnmapMemory(context.GetDevice(), m_VkDeviceMemory);
		return true;
	}

	void VulkanBuffer::Destroy(const VulkanContext& context)
	{
		if (m_VkDeviceMemory != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(context.GetDevice(), m_VkBuffer, nullptr);
			vkFreeMemory(context.GetDevice(), m_VkDeviceMemory, nullptr);
			m_VkBuffer = VK_NULL_HANDLE;
			m_VkDeviceMemory = VK_NULL_HANDLE;
			m_nByteSize = 0;
		}
	}
}

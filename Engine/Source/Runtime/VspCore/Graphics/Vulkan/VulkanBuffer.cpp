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

	VulkanBuffer::VulkanBuffer(VulkanBuffer&& Other) noexcept
		: m_VkBuffer(Other.m_VkBuffer)
		, m_VkDeviceMemory(Other.m_VkDeviceMemory)
		, m_nByteSize(Other.m_nByteSize)
	{
		Other.m_VkBuffer = VK_NULL_HANDLE;
		Other.m_VkDeviceMemory = VK_NULL_HANDLE;
		Other.m_nByteSize = 0;
	}

	VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& Other) noexcept
	{
		if (this != &Other)
		{
			m_VkBuffer = Other.m_VkBuffer;
			m_VkDeviceMemory = Other.m_VkDeviceMemory;
			m_nByteSize = Other.m_nByteSize;

			Other.m_VkBuffer = VK_NULL_HANDLE;
			Other.m_VkDeviceMemory = VK_NULL_HANDLE;
			Other.m_nByteSize = 0;
		}
		return *this;
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

	bool VulkanBuffer::WriteData(
		const VulkanContext& context,
		VkDeviceSize nByteOffset,
		const void* pData,
		VkDeviceSize nByteCount)
	{
		if (!IsValid())
		{
			LOG_ERROR(kLogTag, "Cannot write to an unallocated buffer.");
			return false;
		}
		if (pData == nullptr || nByteCount == 0 || nByteOffset + nByteCount > m_nByteSize)
		{
			LOG_ERROR(kLogTag, "Invalid data range for buffer write.");
			return false;
		}

		void* pMappedData = nullptr;
		if (vkMapMemory(context.GetDevice(), m_VkDeviceMemory, nByteOffset, nByteCount, 0, &pMappedData) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkMapMemory failed.");
			return false;
		}

		memcpy(pMappedData, pData, static_cast<size_t>(nByteCount));
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

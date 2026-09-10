#include "RuntimePCH.h"

#include <cstring>

#include "Core/Logging/Log.h"
#include "Graphics/Vulkan/VulkanImage.h"
#include "Graphics/Vulkan/VulkanRHI.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "VulkanImage";

	VulkanImage::~VulkanImage()
	{
		// See VulkanBuffer: Destroy() is the explicit teardown path; the
		// context always outlives the image.
	}

	bool VulkanImage::Create(
		const VulkanContext& context,
		uint32 uWidth,
		uint32 uHeight,
		const void* pPixelDataRgba8)
	{
		if (IsValid())
		{
			LOG_ERROR(kLogTag, "VulkanImage is already created.");
			return false;
		}
		if (uWidth == 0 || uHeight == 0 || pPixelDataRgba8 == nullptr)
		{
			LOG_ERROR(kLogTag, "Invalid texture dimensions or pixel data.");
			return false;
		}

		const VkDevice device = context.GetDevice();
		const VkDeviceSize k_nImageByteSize = static_cast<VkDeviceSize>(uWidth) * uHeight * 4;

		// Staging buffer with the pixel data.
		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
		context.AllocateBuffer(
			k_nImageByteSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			stagingBuffer,
			stagingBufferMemory);
		if (stagingBuffer == VK_NULL_HANDLE)
		{
			return false;
		}

		void* pMappedData = nullptr;
		vkMapMemory(device, stagingBufferMemory, 0, k_nImageByteSize, 0, &pMappedData);
		memcpy(pMappedData, pPixelDataRgba8, static_cast<size_t>(k_nImageByteSize));
		vkUnmapMemory(device, stagingBufferMemory);

		// Device image.
		VkImageCreateInfo imageCreateInfo = {};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.extent.width = uWidth;
		imageCreateInfo.extent.height = uHeight;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		if (vkCreateImage(device, &imageCreateInfo, nullptr, &m_VkImage) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreateImage failed.");
			vkDestroyBuffer(device, stagingBuffer, nullptr);
			vkFreeMemory(device, stagingBufferMemory, nullptr);
			return false;
		}

		VkMemoryRequirements memoryRequirements = {};
		vkGetImageMemoryRequirements(device, m_VkImage, &memoryRequirements);

		const uint32 nMemoryTypeIndex =
			context.FindMemoryTypeIndex(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (nMemoryTypeIndex == UINT32_MAX)
		{
			LOG_ERROR(kLogTag, "No DEVICE_LOCAL memory type for the texture image.");
			vkDestroyBuffer(device, stagingBuffer, nullptr);
			vkFreeMemory(device, stagingBufferMemory, nullptr);
			return false;
		}

		VkMemoryAllocateInfo allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = memoryRequirements.size;
		allocateInfo.memoryTypeIndex = nMemoryTypeIndex;

		if (vkAllocateMemory(device, &allocateInfo, nullptr, &m_VkImageMemory) != VK_SUCCESS ||
			vkBindImageMemory(device, m_VkImage, m_VkImageMemory, 0) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "Failed to allocate/bind texture image memory.");
			vkDestroyBuffer(device, stagingBuffer, nullptr);
			vkFreeMemory(device, stagingBufferMemory, nullptr);
			return false;
		}

		// Transition UNDEFINED -> TRANSFER_DST, copy, TRANSFER_DST -> SHADER_READ_ONLY.
		if (!TransitionImageLayout(context, m_VkImage,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
		{
			vkDestroyBuffer(device, stagingBuffer, nullptr);
			vkFreeMemory(device, stagingBufferMemory, nullptr);
			return false;
		}

		VkCommandBuffer commandBuffer = context.BeginOneTimeCommandBuffer();
		if (commandBuffer == VK_NULL_HANDLE)
		{
			vkDestroyBuffer(device, stagingBuffer, nullptr);
			vkFreeMemory(device, stagingBufferMemory, nullptr);
			return false;
		}

		VkBufferImageCopy copyRegion = {};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;
		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageOffset = { 0, 0, 0 };
		copyRegion.imageExtent = { uWidth, uHeight, 1 };

		vkCmdCopyBufferToImage(
			commandBuffer,
			stagingBuffer,
			m_VkImage,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&copyRegion);

		const bool bCopySuccess = context.EndOneTimeCommandBuffer(commandBuffer);

		vkDestroyBuffer(device, stagingBuffer, nullptr);
		vkFreeMemory(device, stagingBufferMemory, nullptr);

		if (!bCopySuccess ||
			!TransitionImageLayout(context, m_VkImage,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
		{
			return false;
		}

		// Image view.
		VkImageViewCreateInfo viewCreateInfo = {};
		viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCreateInfo.image = m_VkImage;
		viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewCreateInfo.subresourceRange.baseMipLevel = 0;
		viewCreateInfo.subresourceRange.levelCount = 1;
		viewCreateInfo.subresourceRange.baseArrayLayer = 0;
		viewCreateInfo.subresourceRange.layerCount = 1;

		if (vkCreateImageView(device, &viewCreateInfo, nullptr, &m_VkImageView) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreateImageView failed.");
			return false;
		}

		// Sampler.
		VkSamplerCreateInfo samplerCreateInfo = {};
		samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.anisotropyEnable = VK_FALSE;
		samplerCreateInfo.maxAnisotropy = 1.0f;
		samplerCreateInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		samplerCreateInfo.unnormalizedCoordinates = VK_FALSE;
		samplerCreateInfo.compareEnable = VK_FALSE;
		samplerCreateInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCreateInfo.mipLodBias = 0.0f;
		samplerCreateInfo.minLod = 0.0f;
		samplerCreateInfo.maxLod = 0.0f;

		if (vkCreateSampler(device, &samplerCreateInfo, nullptr, &m_VkSampler) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreateSampler failed.");
			return false;
		}

		LOG_INFO(kLogTag, "Texture created ({}x{}).", uWidth, uHeight);
		return true;
	}

	void VulkanImage::Destroy(const VulkanContext& context)
	{
		const VkDevice device = context.GetDevice();

		if (m_VkSampler != VK_NULL_HANDLE) { vkDestroySampler(device, m_VkSampler, nullptr); m_VkSampler = VK_NULL_HANDLE; }
		if (m_VkImageView != VK_NULL_HANDLE) { vkDestroyImageView(device, m_VkImageView, nullptr); m_VkImageView = VK_NULL_HANDLE; }
		if (m_VkImageMemory != VK_NULL_HANDLE) { vkDestroyImage(device, m_VkImage, nullptr); vkFreeMemory(device, m_VkImageMemory, nullptr); m_VkImageMemory = VK_NULL_HANDLE; m_VkImage = VK_NULL_HANDLE; }
	}

	bool VulkanImage::TransitionImageLayout(
		const VulkanContext& context,
		VkImage image,
		VkImageLayout eOldLayout,
		VkImageLayout eNewLayout)
	{
		VkCommandBuffer commandBuffer = context.BeginOneTimeCommandBuffer();
		if (commandBuffer == VK_NULL_HANDLE)
		{
			return false;   // The context already logged the failure.
		}

		VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

		VkImageMemoryBarrier barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = eOldLayout;
		barrier.newLayout = eNewLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		if (eOldLayout == VK_IMAGE_LAYOUT_UNDEFINED && eNewLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (eOldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && eNewLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		}
		else
		{
			LOG_ERROR(kLogTag, "Unsupported image layout transition.");
			return false;
		}

		vkCmdPipelineBarrier(
			commandBuffer,
			sourceStage,
			destinationStage,
			0,
			0, nullptr,
			0, nullptr,
			1, &barrier);

		return context.EndOneTimeCommandBuffer(commandBuffer);
	}
}

#include "RuntimePCH.h"

#include <cstdio>
#include <cstring>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Core/Logging/Log.h"
#include "Graphics/Vulkan/VulkanRenderer2D.h"
#include "Graphics/Vulkan/Shaders/ShaderBinary.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "VulkanRenderer2D";

	// The acceptance triangle: one vertex per corner, distinct vertex colors.
	static constexpr VulkanRenderer2D::Vertex2D k_sTriangleVertices[] =
	{
		// bottom-left (red)    bottom-right (green)  top (blue)
		{ -0.5f, -0.45f,  1.0f, 0.0f, 0.0f, 1.0f,  0.0f, 0.0f },
		{  0.5f, -0.45f,  0.0f, 1.0f, 0.0f, 1.0f,  1.0f, 0.0f },
		{  0.0f,  0.55f,  0.0f, 0.0f, 1.0f, 1.0f,  0.5f, 1.0f },
	};
	static constexpr size_t k_nTriangleVertexCount = 3;

	// -------------------------------------------------------------------------
	// Lifecycle
	// -------------------------------------------------------------------------

	VulkanRenderer2D::~VulkanRenderer2D()
	{
		Shutdown();
	}

	bool VulkanRenderer2D::Initialize(void* pNativeWindowHandle)
	{

		if (!m_Context.Initialize("Vsp Engine", pNativeWindowHandle))
		{
			// Device below Vulkan 1.2 lands here: the context has already
			// logged the "Unsupported device" details through the Log module.
			return false;
		}

		if (!m_SwapChain.Initialize(m_Context, 1280, 720)) return false;
		if (!CreateFrameResources()) return false;
		if (!CreateTriangleGeometry()) return false;
		if (!CreateDemoTexture()) return false;

		// Descriptor set layout first, then the pipeline that references it.
		// Bindless is the only supported implementation.
		if (!CreateBindlessDescriptors()) return false;
		if (!CreatePipelines()) return false;

		m_bIsInitialized = true;
		LOG_INFO(kLogTag, "Renderer ready (feature path: Vulkan 1.3 bindless).");
		return true;
	}

	void VulkanRenderer2D::Shutdown()
	{
		if (m_Context.IsInitialized())
		{
			vkDeviceWaitIdle(m_Context.GetDevice());
		}

		DestroyFrameResources();
		DestroyTriangleGeometry();
		DestroyDemoTexture();
		DestroyDescriptors();
		DestroyPipelines();

		m_SwapChain.Destroy();
		m_Context.Destroy();
		m_bIsInitialized = false;
	}

	void VulkanRenderer2D::OnWindowResize(uint32 uWidth, uint32 uHeight)
	{
		if (!m_bIsInitialized)
		{
			return;
		}

		m_bIsMinimized = (uWidth == 0 || uHeight == 0);
		if (m_bIsMinimized)
		{
			return;
		}

		if (!m_SwapChain.Recreate(uWidth, uHeight))
		{
			// The recreate failure was already logged inside the swapchain.
		}
	}

	void VulkanRenderer2D::SetTrianglePosition(float fPositionX, float fPositionY)
	{
		m_fTrianglePositionX = fPositionX;
		m_fTrianglePositionY = fPositionY;
	}

	void VulkanRenderer2D::SetColorMode(int32 nColorMode)
	{
		m_nColorMode = nColorMode;
	}

	// -------------------------------------------------------------------------
	// FrameResources (command buffers, per-frame uniforms, sync primitives)
	// -------------------------------------------------------------------------

	bool VulkanRenderer2D::CreateFrameResources()
	{
		const VkDevice device = m_Context.GetDevice();

		VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
		commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		commandBufferAllocateInfo.commandPool = m_Context.GetCommandPool();
		commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		commandBufferAllocateInfo.commandBufferCount = k_nMaxFramesInFlight;

		VkCommandBuffer commandBuffers[k_nMaxFramesInFlight] = {};
		if (vkAllocateCommandBuffers(device, &commandBufferAllocateInfo, commandBuffers) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkAllocateCommandBuffers failed.");
			return false;
		}

		VkSemaphoreCreateInfo semaphoreCreateInfo = {};
		semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceCreateInfo = {};
		fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;   // First frame passes the wait.

		for (uint32 uFrameIndex = 0; uFrameIndex < k_nMaxFramesInFlight; ++uFrameIndex)
		{
			FrameResources& frame = m_Frames[uFrameIndex];
			frame.commandBuffer = commandBuffers[uFrameIndex];

			if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frame.imageAvailableSemaphore) != VK_SUCCESS ||
				vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frame.renderFinishedSemaphore) != VK_SUCCESS ||
				vkCreateFence(device, &fenceCreateInfo, nullptr, &frame.inFlightFence) != VK_SUCCESS)
			{
				LOG_ERROR(kLogTag, "Failed to create frame sync objects.");
				return false;
			}

			// One uniform buffer per frame so the CPU never races the GPU.
			m_Context.AllocateBuffer(
				sizeof(CameraUniformData),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				frame.uniformBuffer,
				frame.uniformBufferMemory);
			if (frame.uniformBuffer == VK_NULL_HANDLE)
			{
				return false;
			}
		}
		return true;
	}

	void VulkanRenderer2D::DestroyFrameResources()
	{
		if (!m_Context.IsInitialized())
		{
			return;
		}

		const VkDevice device = m_Context.GetDevice();

		for (uint32 uFrameIndex = 0; uFrameIndex < k_nMaxFramesInFlight; ++uFrameIndex)
		{
			FrameResources& frame = m_Frames[uFrameIndex];

			if (frame.inFlightFence != VK_NULL_HANDLE) { vkDestroyFence(device, frame.inFlightFence, nullptr); frame.inFlightFence = VK_NULL_HANDLE; }
			if (frame.imageAvailableSemaphore != VK_NULL_HANDLE) { vkDestroySemaphore(device, frame.imageAvailableSemaphore, nullptr); frame.imageAvailableSemaphore = VK_NULL_HANDLE; }
			if (frame.renderFinishedSemaphore != VK_NULL_HANDLE) { vkDestroySemaphore(device, frame.renderFinishedSemaphore, nullptr); frame.renderFinishedSemaphore = VK_NULL_HANDLE; }
			if (frame.uniformBufferMemory != VK_NULL_HANDLE) { vkDestroyBuffer(device, frame.uniformBuffer, nullptr); vkFreeMemory(device, frame.uniformBufferMemory, nullptr); frame.uniformBufferMemory = VK_NULL_HANDLE; frame.uniformBuffer = VK_NULL_HANDLE; }
		}

		VkCommandBuffer commandBuffers[k_nMaxFramesInFlight] = {};
		for (uint32 uFrameIndex = 0; uFrameIndex < k_nMaxFramesInFlight; ++uFrameIndex)
		{
			commandBuffers[uFrameIndex] = m_Frames[uFrameIndex].commandBuffer;
		}
		vkFreeCommandBuffers(device, m_Context.GetCommandPool(), k_nMaxFramesInFlight, commandBuffers);
	}

	// -------------------------------------------------------------------------
	// Triangle geometry
	// -------------------------------------------------------------------------

	bool VulkanRenderer2D::CreateTriangleGeometry()
	{
		const VkDeviceSize k_nBufferByteSize = sizeof(k_sTriangleVertices);

		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
		m_Context.AllocateBuffer(
			k_nBufferByteSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			stagingBuffer,
			stagingBufferMemory);
		if (stagingBuffer == VK_NULL_HANDLE)
		{
			return false;
		}

		void* pMappedData = nullptr;
		vkMapMemory(m_Context.GetDevice(), stagingBufferMemory, 0, k_nBufferByteSize, 0, &pMappedData);
		memcpy(pMappedData, k_sTriangleVertices, static_cast<size_t>(k_nBufferByteSize));
		vkUnmapMemory(m_Context.GetDevice(), stagingBufferMemory);

		m_Context.AllocateBuffer(
			k_nBufferByteSize,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			m_VkVertexBuffer,
			m_VkVertexBufferMemory);
		if (m_VkVertexBuffer == VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_Context.GetDevice(), stagingBuffer, nullptr);
			vkFreeMemory(m_Context.GetDevice(), stagingBufferMemory, nullptr);
			return false;
		}

		// Copy staging -> device local through a one-time command buffer.
		VkCommandBuffer commandBuffer = m_Context.BeginOneTimeCommandBuffer();
		if (commandBuffer == VK_NULL_HANDLE)
		{
			return false;
		}

		VkBufferCopy copyRegion = {};
		copyRegion.size = k_nBufferByteSize;
		vkCmdCopyBuffer(commandBuffer, stagingBuffer, m_VkVertexBuffer, 1, &copyRegion);

		const bool bSuccess = m_Context.EndOneTimeCommandBuffer(commandBuffer);

		vkDestroyBuffer(m_Context.GetDevice(), stagingBuffer, nullptr);
		vkFreeMemory(m_Context.GetDevice(), stagingBufferMemory, nullptr);
		return bSuccess;
	}

	void VulkanRenderer2D::DestroyTriangleGeometry()
	{
		if (!m_Context.IsInitialized())
		{
			return;
		}

		if (m_VkVertexBufferMemory != VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_Context.GetDevice(), m_VkVertexBuffer, nullptr);
			vkFreeMemory(m_Context.GetDevice(), m_VkVertexBufferMemory, nullptr);
			m_VkVertexBuffer = VK_NULL_HANDLE;
			m_VkVertexBufferMemory = VK_NULL_HANDLE;
		}
	}

	// -------------------------------------------------------------------------
	// Demo texture (white 8x8 - keeps the acceptance colors exact while the
	// bindless array sampling is exercised on both paths)
	// -------------------------------------------------------------------------

	bool VulkanRenderer2D::CreateDemoTexture()
	{
		constexpr uint32 k_nTextureWidth = 8;
		constexpr uint32 k_nTextureHeight = 8;

		uint8_t texturePixels[k_nTextureWidth * k_nTextureHeight * 4];
		for (uint32 nPixelIndex = 0; nPixelIndex < k_nTextureWidth * k_nTextureHeight; ++nPixelIndex)
		{
			texturePixels[nPixelIndex * 4 + 0] = 255;
			texturePixels[nPixelIndex * 4 + 1] = 255;
			texturePixels[nPixelIndex * 4 + 2] = 255;
			texturePixels[nPixelIndex * 4 + 3] = 255;
		}

		const VkDeviceSize k_nImageByteSize = sizeof(texturePixels);
		const VkDevice device = m_Context.GetDevice();

		// Staging buffer with the pixel data.
		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
		m_Context.AllocateBuffer(
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
		memcpy(pMappedData, texturePixels, sizeof(texturePixels));
		vkUnmapMemory(device, stagingBufferMemory);

		// Device image.
		VkImageCreateInfo imageCreateInfo = {};
		imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.extent.width = k_nTextureWidth;
		imageCreateInfo.extent.height = k_nTextureHeight;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;

		if (vkCreateImage(device, &imageCreateInfo, nullptr, &m_VkTextureImage) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreateImage failed.");
			return false;
		}

		VkMemoryRequirements memoryRequirements = {};
		vkGetImageMemoryRequirements(device, m_VkTextureImage, &memoryRequirements);

		const uint32 nMemoryTypeIndex = m_Context.FindMemoryTypeIndex(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		if (nMemoryTypeIndex == UINT32_MAX)
		{
			LOG_ERROR(kLogTag, "No DEVICE_LOCAL memory type for the demo texture.");
			return false;
		}

		VkMemoryAllocateInfo allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = memoryRequirements.size;
		allocateInfo.memoryTypeIndex = nMemoryTypeIndex;

		if (vkAllocateMemory(device, &allocateInfo, nullptr, &m_VkTextureMemory) != VK_SUCCESS ||
			vkBindImageMemory(device, m_VkTextureImage, m_VkTextureMemory, 0) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "Failed to allocate/bind demo texture memory.");
			return false;
		}

		// Transition UNDEFINED -> TRANSFER_DST, copy, TRANSFER_DST -> SHADER_READ_ONLY.
		if (!TransitionImageLayout(m_Context, m_VkTextureImage, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL))
		{
			return false;
		}

		VkCommandBuffer commandBuffer = m_Context.BeginOneTimeCommandBuffer();
		if (commandBuffer == VK_NULL_HANDLE)
		{
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
		copyRegion.imageExtent = { k_nTextureWidth, k_nTextureHeight, 1 };

		vkCmdCopyBufferToImage(
			commandBuffer,
			stagingBuffer,
			m_VkTextureImage,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&copyRegion);

		const bool bCopySuccess = m_Context.EndOneTimeCommandBuffer(commandBuffer);

		vkDestroyBuffer(device, stagingBuffer, nullptr);
		vkFreeMemory(device, stagingBufferMemory, nullptr);

		if (!bCopySuccess ||
			!TransitionImageLayout(m_Context, m_VkTextureImage, VK_FORMAT_R8G8B8A8_UNORM,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL))
		{
			return false;
		}

		// Image view.
		VkImageViewCreateInfo viewCreateInfo = {};
		viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCreateInfo.image = m_VkTextureImage;
		viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewCreateInfo.subresourceRange.baseMipLevel = 0;
		viewCreateInfo.subresourceRange.levelCount = 1;
		viewCreateInfo.subresourceRange.baseArrayLayer = 0;
		viewCreateInfo.subresourceRange.layerCount = 1;

		if (vkCreateImageView(device, &viewCreateInfo, nullptr, &m_VkTextureView) != VK_SUCCESS)
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

		if (vkCreateSampler(device, &samplerCreateInfo, nullptr, &m_VkTextureSampler) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreateSampler failed.");
			return false;
		}

		LOG_INFO(kLogTag, "Demo texture uploaded ({}x{}, used by both feature paths).", k_nTextureWidth, k_nTextureHeight);
		return true;
	}

	void VulkanRenderer2D::DestroyDemoTexture()
	{
		if (!m_Context.IsInitialized())
		{
			return;
		}

		const VkDevice device = m_Context.GetDevice();

		if (m_VkTextureSampler != VK_NULL_HANDLE) { vkDestroySampler(device, m_VkTextureSampler, nullptr); m_VkTextureSampler = VK_NULL_HANDLE; }
		if (m_VkTextureView != VK_NULL_HANDLE) { vkDestroyImageView(device, m_VkTextureView, nullptr); m_VkTextureView = VK_NULL_HANDLE; }
		if (m_VkTextureMemory != VK_NULL_HANDLE) { vkDestroyImage(device, m_VkTextureImage, nullptr); vkFreeMemory(device, m_VkTextureMemory, nullptr); m_VkTextureMemory = VK_NULL_HANDLE; m_VkTextureImage = VK_NULL_HANDLE; }
	}

	// -------------------------------------------------------------------------
	// Image layout transitions
	// -------------------------------------------------------------------------

	bool VulkanRenderer2D::TransitionImageLayout(
		const VulkanContext& context,
		VkImage image,
		VkFormat format,
		VkImageLayout oldLayout,
		VkImageLayout newLayout)
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
		barrier.oldLayout = oldLayout;
		barrier.newLayout = newLayout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
		{
			barrier.srcAccessMask = 0;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		}
		else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
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

	// -------------------------------------------------------------------------
	// Descriptors (bindless)
	// -------------------------------------------------------------------------

	bool VulkanRenderer2D::CreateBindlessDescriptors()
	{
		const VkDevice device = m_Context.GetDevice();

		// Layout: binding 0 = per-frame camera UBO (vertex),
		//         binding 1 = bindless combined-image-sampler array (fragment).
		// GLSL "sampler2D" maps to VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER.
		VkDescriptorSetLayoutBinding bindings[2] = {};
		bindings[0].binding = 0;
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		bindings[1].binding = 1;
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		bindings[1].descriptorCount = k_nMaxBindlessTextureCount;
		bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		// One flag entry per layout binding: binding 0 has no flags, binding 1
		// is partially bound (only slot 0 is written).
		VkDescriptorBindingFlags bindingFlags[2] =
		{
			0,
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
		};

		VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo = {};
		bindingFlagsCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		bindingFlagsCreateInfo.bindingCount = 2;
		bindingFlagsCreateInfo.pBindingFlags = bindingFlags;

		VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {};
		layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutCreateInfo.pNext = &bindingFlagsCreateInfo;
		layoutCreateInfo.bindingCount = 2;
		layoutCreateInfo.pBindings = bindings;

		if (vkCreateDescriptorSetLayout(device, &layoutCreateInfo, nullptr, &m_VkDescriptorSetLayout) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "Failed to create bindless descriptor set layout.");
			return false;
		}

		// Pool: k_nMaxFramesInFlight sets. Each bindless set carries a 4096-slot
		// sampled-image array, so the pool limit must cover every slot.
		VkDescriptorPoolSize poolSizes[2] = {};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = k_nMaxFramesInFlight;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = k_nMaxFramesInFlight * k_nMaxBindlessTextureCount;

		VkDescriptorPoolCreateInfo poolCreateInfo = {};
		poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolCreateInfo.maxSets = k_nMaxFramesInFlight;
		poolCreateInfo.poolSizeCount = 2;
		poolCreateInfo.pPoolSizes = poolSizes;

		if (vkCreateDescriptorPool(device, &poolCreateInfo, nullptr, &m_VkDescriptorPool) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "Failed to create bindless descriptor pool.");
			return false;
		}

		VkDescriptorSetLayout layouts[k_nMaxFramesInFlight] =
		{
			m_VkDescriptorSetLayout,
			m_VkDescriptorSetLayout,
		};

		VkDescriptorSetAllocateInfo allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateInfo.descriptorPool = m_VkDescriptorPool;
		allocateInfo.descriptorSetCount = k_nMaxFramesInFlight;
		allocateInfo.pSetLayouts = layouts;

		if (vkAllocateDescriptorSets(device, &allocateInfo, m_VkDescriptorSets) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "Failed to allocate bindless descriptor sets.");
			return false;
		}

		// Write per-frame UBOs + the single white texture into slot 0 of the
		// bindless array (partially bound: all other slots stay uninitialized).
		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = m_VkTextureView;
		imageInfo.sampler = m_VkTextureSampler;

		for (uint32 uFrameIndex = 0; uFrameIndex < k_nMaxFramesInFlight; ++uFrameIndex)
		{
			VkDescriptorBufferInfo bufferInfo = {};
			bufferInfo.buffer = m_Frames[uFrameIndex].uniformBuffer;
			bufferInfo.offset = 0;
			bufferInfo.range = sizeof(CameraUniformData);

			VkWriteDescriptorSet writes[2] = {};
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].dstSet = m_VkDescriptorSets[uFrameIndex];
			writes[0].dstBinding = 0;
			writes[0].dstArrayElement = 0;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writes[0].descriptorCount = 1;
			writes[0].pBufferInfo = &bufferInfo;

			writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[1].dstSet = m_VkDescriptorSets[uFrameIndex];
			writes[1].dstBinding = 1;
			writes[1].dstArrayElement = 0;
			writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[1].descriptorCount = 1;   // Slot 0 only; the rest stays partially bound.
			writes[1].pImageInfo = &imageInfo;

			vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
		}

		LOG_INFO(kLogTag, "Bindless descriptors ready ({} sampled-image slots).", k_nMaxBindlessTextureCount);
		return true;
	}

	void VulkanRenderer2D::DestroyDescriptors()
	{
		if (!m_Context.IsInitialized())
		{
			return;
		}

		const VkDevice device = m_Context.GetDevice();

		if (m_VkDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device, m_VkDescriptorPool, nullptr); m_VkDescriptorPool = VK_NULL_HANDLE; }
		if (m_VkDescriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, m_VkDescriptorSetLayout, nullptr); m_VkDescriptorSetLayout = VK_NULL_HANDLE; }
	}

	// -------------------------------------------------------------------------
	// Pipelines
	// -------------------------------------------------------------------------

	bool VulkanRenderer2D::CreatePipelines()
	{
		const VkDevice device = m_Context.GetDevice();

		// The bindless implementation is the only supported path.
		// Note: the generated header stores the SPIR-V size in uint32 words;
		// vkCreateShaderModule expects bytes.
		VkShaderModule vertexShader = m_Context.CreateShaderModule(
			Shaders::k_TriangleBindless_vertSpv,
			Shaders::k_nTriangleBindless_vertSpvSize * sizeof(uint32));
		VkShaderModule fragmentShader = m_Context.CreateShaderModule(
			Shaders::k_TriangleBindless_fragSpv,
			Shaders::k_nTriangleBindless_fragSpvSize * sizeof(uint32));

		if (vertexShader == VK_NULL_HANDLE || fragmentShader == VK_NULL_HANDLE)
		{
			return false;
		}

		// Push constants: color override + mode + texture index (fragment stage).
		VkPushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(PushConstants);

		VkPipelineLayoutCreateInfo layoutCreateInfo = {};
		layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutCreateInfo.setLayoutCount = 1;
		layoutCreateInfo.pSetLayouts = &m_VkDescriptorSetLayout;
		layoutCreateInfo.pushConstantRangeCount = 1;
		layoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		if (vkCreatePipelineLayout(device, &layoutCreateInfo, nullptr, &m_VkPipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreatePipelineLayout failed.");
			vkDestroyShaderModule(device, vertexShader, nullptr);
			vkDestroyShaderModule(device, fragmentShader, nullptr);
			return false;
		}

		const bool bPipelineCreated = CreateGraphicsPipeline(
			vertexShader, fragmentShader, m_VkPipelineLayout, m_VkPipeline);

		vkDestroyShaderModule(device, vertexShader, nullptr);
		vkDestroyShaderModule(device, fragmentShader, nullptr);

		if (!bPipelineCreated)
		{
			// CreateGraphicsPipeline already logged the failure details.
			return false;
		}
		return true;
	}

	bool VulkanRenderer2D::CreateGraphicsPipeline(
		VkShaderModule vertexShader,
		VkShaderModule fragmentShader,
		VkPipelineLayout pipelineLayout,
		VkPipeline& outPipeline) const
	{
		const VkDevice device = m_Context.GetDevice();

		VkPipelineShaderStageCreateInfo shaderStages[2] = {};
		shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		shaderStages[0].module = vertexShader;
		shaderStages[0].pName = "main";

		shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderStages[1].module = fragmentShader;
		shaderStages[1].pName = "main";

		// Vertex input: one binding, position + color + uv.
		VkVertexInputBindingDescription vertexBinding = {};
		vertexBinding.binding = 0;
		vertexBinding.stride = sizeof(Vertex2D);
		vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription vertexAttributes[3] = {};
		vertexAttributes[0].location = 0;
		vertexAttributes[0].binding = 0;
		vertexAttributes[0].format = VK_FORMAT_R32G32_SFLOAT;
		vertexAttributes[0].offset = offsetof(Vertex2D, fPositionX);

		vertexAttributes[1].location = 1;
		vertexAttributes[1].binding = 0;
		vertexAttributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		vertexAttributes[1].offset = offsetof(Vertex2D, fColorR);

		vertexAttributes[2].location = 2;
		vertexAttributes[2].binding = 0;
		vertexAttributes[2].format = VK_FORMAT_R32G32_SFLOAT;
		vertexAttributes[2].offset = offsetof(Vertex2D, fUvU);

		VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &vertexBinding;
		vertexInputInfo.vertexAttributeDescriptionCount = 3;
		vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		// Dynamic viewport/scissor keep resize handling trivial.
		VkPipelineViewportStateCreateInfo viewportState = {};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		VkPipelineDynamicStateCreateInfo dynamicState = {};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		dynamicState.dynamicStateCount = 2;
		dynamicState.pDynamicStates = dynamicStates;

		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_NONE;
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;

		VkPipelineMultisampleStateCreateInfo multisampling = {};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
		colorBlendAttachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = VK_TRUE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

		VkPipelineColorBlendStateCreateInfo colorBlending = {};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
		pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCreateInfo.stageCount = 2;
		pipelineCreateInfo.pStages = shaderStages;
		pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
		pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pRasterizationState = &rasterizer;
		pipelineCreateInfo.pMultisampleState = &multisampling;
		pipelineCreateInfo.pColorBlendState = &colorBlending;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.layout = pipelineLayout;
		pipelineCreateInfo.renderPass = m_SwapChain.GetRenderPass();
		pipelineCreateInfo.subpass = 0;
		pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
		pipelineCreateInfo.basePipelineIndex = -1;

		const VkResult eResult = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &outPipeline);
		if (eResult != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreateGraphicsPipelines failed (VkResult {}).", static_cast<int32>(eResult));
			return false;
		}
		return true;
	}

	void VulkanRenderer2D::DestroyPipelines()
	{
		if (!m_Context.IsInitialized())
		{
			return;
		}

		const VkDevice device = m_Context.GetDevice();

		if (m_VkPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_VkPipeline, nullptr); m_VkPipeline = VK_NULL_HANDLE; }
		if (m_VkPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, m_VkPipelineLayout, nullptr); m_VkPipelineLayout = VK_NULL_HANDLE; }
	}

	// -------------------------------------------------------------------------
	// Frame rendering
	// -------------------------------------------------------------------------

	void VulkanRenderer2D::UpdateCameraUniform(uint32 uFrameIndex)
	{
		// Orthographic projection preserving the window aspect ratio; the
		// triangle lives in [-1, 1] on the shorter axis. Vulkan's viewport
		// transform maps NDC +Y to the BOTTOM of the framebuffer (it uses
		// (y+1)/2, unlike OpenGL), so the ortho's top/bottom are swapped:
		// world +Y ends up at NDC -1, which is the top of the screen.
		const VkExtent2D extent = m_SwapChain.GetExtent();
		const float fAspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);

		glm::mat4 projectionMatrix = glm::ortho(-fAspectRatio, fAspectRatio, 1.0f, -1.0f, -1.0f, 1.0f);
		glm::mat4 viewMatrix = glm::translate(
			glm::mat4(1.0f),
			glm::vec3(m_fTrianglePositionX, m_fTrianglePositionY, 0.0f));
		const glm::mat4 viewProjectionMatrix = projectionMatrix * viewMatrix;

		CameraUniformData uniformData;
		memcpy(uniformData.m4ViewProjection, &viewProjectionMatrix[0][0], sizeof(uniformData.m4ViewProjection));

		FrameResources& frame = m_Frames[uFrameIndex];
		void* pMappedData = nullptr;
		vkMapMemory(m_Context.GetDevice(), frame.uniformBufferMemory, 0, sizeof(CameraUniformData), 0, &pMappedData);
		memcpy(pMappedData, &uniformData, sizeof(CameraUniformData));
		vkUnmapMemory(m_Context.GetDevice(), frame.uniformBufferMemory);
	}

	void VulkanRenderer2D::BuildPushConstants(PushConstants& outPushConstants) const
	{
		outPushConstants = {};
		outPushConstants.uTextureIndex = 0;   // Demo texture lives in slot 0.
		outPushConstants.nColorMode = m_nColorMode;

		switch (m_nColorMode)
		{
		case 0:   // Red
			outPushConstants.fOverrideColorR = 1.0f;
			outPushConstants.fOverrideColorG = 0.0f;
			outPushConstants.fOverrideColorB = 0.0f;
			outPushConstants.fOverrideColorA = 1.0f;
			break;
		case 1:   // Blue
			outPushConstants.fOverrideColorR = 0.0f;
			outPushConstants.fOverrideColorG = 0.0f;
			outPushConstants.fOverrideColorB = 1.0f;
			outPushConstants.fOverrideColorA = 1.0f;
			break;
		case 2:   // Green
			outPushConstants.fOverrideColorR = 0.0f;
			outPushConstants.fOverrideColorG = 1.0f;
			outPushConstants.fOverrideColorB = 0.0f;
			outPushConstants.fOverrideColorA = 1.0f;
			break;
		default:   // MultiColor - vertex colors are used, override ignored.
			outPushConstants.fOverrideColorR = 0.0f;
			outPushConstants.fOverrideColorG = 0.0f;
			outPushConstants.fOverrideColorB = 0.0f;
			outPushConstants.fOverrideColorA = 0.0f;
			break;
		}
	}

	void VulkanRenderer2D::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32 uImageIndex)
	{
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		const VkExtent2D extent = m_SwapChain.GetExtent();

		VkRenderPassBeginInfo renderPassBeginInfo = {};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = m_SwapChain.GetRenderPass();
		renderPassBeginInfo.framebuffer = m_SwapChain.GetFramebuffer(uImageIndex);
		renderPassBeginInfo.renderArea.offset = { 0, 0 };
		renderPassBeginInfo.renderArea.extent = extent;

		VkClearValue clearValue = {};
		clearValue.color = { { 0.06f, 0.06f, 0.10f, 1.0f } };
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = &clearValue;

		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Viewport/scissor are dynamic states.
		VkViewport viewport = {};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(extent.width);
		viewport.height = static_cast<float>(extent.height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = extent;
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		PushConstants pushConstants;
		BuildPushConstants(pushConstants);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_VkPipeline);
		vkCmdBindDescriptorSets(
			commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_VkPipelineLayout,
			0,
			1,
			&m_VkDescriptorSets[m_nCurrentFrameIndex],
			0,
			nullptr);
		vkCmdPushConstants(
			commandBuffer,
			m_VkPipelineLayout,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			0,
			sizeof(PushConstants),
			&pushConstants);

		const VkDeviceSize k_nVertexBufferOffset = 0;
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_VkVertexBuffer, &k_nVertexBufferOffset);
		vkCmdDraw(commandBuffer, static_cast<uint32>(k_nTriangleVertexCount), 1, 0, 0);

		vkCmdEndRenderPass(commandBuffer);
		vkEndCommandBuffer(commandBuffer);
	}

	bool VulkanRenderer2D::RenderFrame()
	{
		if (!m_bIsInitialized || m_bIsMinimized)
		{
			return true;   // Nothing to do while minimized.
		}

		const VkExtent2D frameExtent = m_SwapChain.GetExtent();
		if (frameExtent.width == 0 || frameExtent.height == 0)
		{
			return true;   // Surface not ready (e.g. window still initializing).
		}

		FrameResources& frame = m_Frames[m_nCurrentFrameIndex];

		vkWaitForFences(m_Context.GetDevice(), 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);
		vkResetFences(m_Context.GetDevice(), 1, &frame.inFlightFence);

		const SwapChainAcquireResult eAcquireResult =
			m_SwapChain.AcquireNextImage(frame.imageAvailableSemaphore);
		if (eAcquireResult == SwapChainAcquireResult::Failed)
		{
			return false;   // The acquire failure was already logged inside the swapchain.
		}
		if (eAcquireResult == SwapChainAcquireResult::OutOfDate)
		{
			// Out of date (resized): rebuild and retry next frame.
			VkExtent2D extent = m_SwapChain.GetExtent();
			if (!m_SwapChain.Recreate(extent.width, extent.height))
			{
				return false;
			}
			return true;
		}

		UpdateCameraUniform(m_nCurrentFrameIndex);

		vkResetCommandBuffer(frame.commandBuffer, 0);
		RecordCommandBuffer(frame.commandBuffer, m_SwapChain.GetCurrentImageIndex());

		bool bIsOutOfDate = false;
		if (!m_SwapChain.SubmitAndPresent(
			frame.commandBuffer,
			frame.imageAvailableSemaphore,
			frame.renderFinishedSemaphore,
			frame.inFlightFence,
			bIsOutOfDate))
		{
			return false;
		}

		if (bIsOutOfDate)
		{
			VkExtent2D extent = m_SwapChain.GetExtent();
			m_SwapChain.Recreate(extent.width, extent.height);
		}

		m_nCurrentFrameIndex = (m_nCurrentFrameIndex + 1) % k_nMaxFramesInFlight;
		return true;
	}

	// -------------------------------------------------------------------------
	// Framebuffer capture (acceptance-test support)
	// -------------------------------------------------------------------------

	// Minimal 32-bit BMP writer: header + bottom-up BGRA rows. The swapchain
	// format is B8G8R8A8, so the bytes map 1:1 onto BMP pixels.
	#pragma pack(push, 1)
	struct BmpFileHeader
	{
		uint16_t uType = 0x4D42;
		uint32 uFileByteSize = 0;
		uint16_t uReserved1 = 0;
		uint16_t uReserved2 = 0;
		uint32 uPixelDataOffset = 54;
	};

	struct BmpInfoHeader
	{
		uint32 uHeaderByteSize = 40;
		int32 nWidth = 0;
		int32 nHeight = 0;
		uint16_t uPlaneCount = 1;
		uint16_t uBitCount = 32;
		uint32 uCompression = 0;
		uint32 uImageByteSize = 0;
		int32 nPixelsPerMeterX = 0;
		int32 nPixelsPerMeterY = 0;
		uint32 uColorCount = 0;
		uint32 uImportantColorCount = 0;
	};
	#pragma pack(pop)

	bool VulkanRenderer2D::CaptureFramebuffer(const VspString& sFilePath)
	{
		if (!m_bIsInitialized)
		{
			LOG_ERROR(kLogTag, "Renderer is not initialized.");
			return false;
		}

		const VkDevice device = m_Context.GetDevice();
		vkDeviceWaitIdle(device);

		// Acquire an image from the presentation engine with a fence: the fence
		// is signaled once the image's previous present has fully completed, so
		// its content is guaranteed to be available for reading.
		VkFence acquireFence = VK_NULL_HANDLE;
		{
			VkFenceCreateInfo fenceCreateInfo = {};
			fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			vkCreateFence(device, &fenceCreateInfo, nullptr, &acquireFence);
		}

		uint32 uImageIndex = 0;
		const VkResult eAcquireResult = vkAcquireNextImageKHR(
			device,
			m_SwapChain.GetSwapchain(),
			UINT64_MAX,
			VK_NULL_HANDLE,
			acquireFence,
			&uImageIndex);
		if (eAcquireResult != VK_SUCCESS)
		{
			vkDestroyFence(device, acquireFence, nullptr);
			LOG_ERROR(kLogTag, "vkAcquireNextImageKHR failed during capture.");
			return false;
		}
		vkWaitForFences(device, 1, &acquireFence, VK_TRUE, UINT64_MAX);
		vkDestroyFence(device, acquireFence, nullptr);

		const VkExtent2D extent = m_SwapChain.GetExtent();
		const VkDeviceSize k_nRowByteSize = static_cast<VkDeviceSize>(extent.width) * 4;
		const VkDeviceSize k_nImageByteSize = k_nRowByteSize * extent.height;
		const VkImage sourceImage = m_SwapChain.GetImage(uImageIndex);

		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
		m_Context.AllocateBuffer(
			k_nImageByteSize,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			stagingBuffer,
			stagingMemory);
		if (stagingBuffer == VK_NULL_HANDLE)
		{
			return false;
		}

		auto TransitionForCopy = [&](VkImageLayout oldLayout, VkImageLayout newLayout,
			VkAccessFlags srcAccess, VkAccessFlags dstAccess,
			VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) -> bool
		{
			VkCommandBuffer commandBuffer = m_Context.BeginOneTimeCommandBuffer();
			if (commandBuffer == VK_NULL_HANDLE)
			{
				return false;
			}

			VkImageMemoryBarrier barrier = {};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = oldLayout;
			barrier.newLayout = newLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = sourceImage;
			barrier.srcAccessMask = srcAccess;
			barrier.dstAccessMask = dstAccess;
			barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barrier.subresourceRange.baseMipLevel = 0;
			barrier.subresourceRange.levelCount = 1;
			barrier.subresourceRange.baseArrayLayer = 0;
			barrier.subresourceRange.layerCount = 1;

			vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
			return m_Context.EndOneTimeCommandBuffer(commandBuffer);
		};

		bool bSuccess = true;
		// After a successful acquire the content is available with no pending
		// accesses: srcAccess = 0, srcStage = TOP_OF_PIPE.
		bSuccess = bSuccess && TransitionForCopy(
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			0, VK_ACCESS_TRANSFER_READ_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

		if (bSuccess)
		{
			VkCommandBuffer commandBuffer = m_Context.BeginOneTimeCommandBuffer();
			if (commandBuffer == VK_NULL_HANDLE)
			{
				bSuccess = false;
			}
			else
			{
				VkBufferImageCopy copyRegion = {};
				copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copyRegion.imageSubresource.layerCount = 1;
				copyRegion.imageExtent = { extent.width, extent.height, 1 };
				vkCmdCopyImageToBuffer(
					commandBuffer,
					sourceImage,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					stagingBuffer,
					1,
					&copyRegion);
				bSuccess = m_Context.EndOneTimeCommandBuffer(commandBuffer);
			}
		}

		bSuccess = bSuccess && TransitionForCopy(
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_ACCESS_TRANSFER_READ_BIT, 0,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

		// Present the acquired image again so the render loop never deadlocks
		// waiting for a free swapchain image.
		if (bSuccess)
		{
			VkSwapchainKHR swapchain = m_SwapChain.GetSwapchain();
			VkPresentInfoKHR presentInfo = {};
			presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
			presentInfo.swapchainCount = 1;
			presentInfo.pSwapchains = &swapchain;
			presentInfo.pImageIndices = &uImageIndex;
			vkQueuePresentKHR(m_Context.GetGraphicsQueue(), &presentInfo);
		}

		if (bSuccess)
		{
			void* pMappedData = nullptr;
			vkMapMemory(device, stagingMemory, 0, k_nImageByteSize, 0, &pMappedData);

			const uint32 uRowByteSize = extent.width * 4;
			const uint32 uImageByteSize = uRowByteSize * extent.height;

			BmpFileHeader fileHeader;
			BmpInfoHeader infoHeader;
			fileHeader.uFileByteSize = sizeof(BmpFileHeader) + sizeof(BmpInfoHeader) + uImageByteSize;
			infoHeader.nWidth = static_cast<int32>(extent.width);
			infoHeader.nHeight = static_cast<int32>(extent.height);
			infoHeader.uImageByteSize = uImageByteSize;

			FILE* pFile = nullptr;
			fopen_s(&pFile, sFilePath.GetData(), "wb");
			if (pFile == nullptr)
			{
				LOG_ERROR(kLogTag, "Failed to open capture file for writing.");
				bSuccess = false;
			}
			else
			{
				fwrite(&fileHeader, sizeof(fileHeader), 1, pFile);
				fwrite(&infoHeader, sizeof(infoHeader), 1, pFile);

				// BMP rows are bottom-up: write them in reverse order.
				const uint8_t* pImageBytes = static_cast<const uint8_t*>(pMappedData);
				for (int32 nRowIndex = static_cast<int32>(extent.height) - 1; nRowIndex >= 0; --nRowIndex)
				{
					fwrite(pImageBytes + static_cast<size_t>(nRowIndex) * uRowByteSize, uRowByteSize, 1, pFile);
				}
				fclose(pFile);
			}

			vkUnmapMemory(device, stagingMemory);
		}

		vkDestroyBuffer(device, stagingBuffer, nullptr);
		vkFreeMemory(device, stagingMemory, nullptr);

		if (!bSuccess)
		{
			LOG_ERROR(kLogTag, "Framebuffer capture failed.");
		}
		return bSuccess;
	}
}


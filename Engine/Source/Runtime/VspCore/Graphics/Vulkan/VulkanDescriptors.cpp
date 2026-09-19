#include "RuntimePCH.h"

#include "Core/Logging/Log.h"
#include "Graphics/ShaderBindings.h"
#include "Graphics/Vulkan/VulkanBuffer.h"
#include "Graphics/Vulkan/VulkanDescriptors.h"
#include "Graphics/Vulkan/VulkanRHI.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "VulkanDescriptors";

	VulkanDescriptors::~VulkanDescriptors()
	{
		// See VulkanBuffer: Destroy() is the explicit teardown path; the
		// context always outlives the descriptor sets.
	}

	bool VulkanDescriptors::CreateSharedSampler(const VulkanContext& context)
	{
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

		if (vkCreateSampler(context.GetDevice(), &samplerCreateInfo, nullptr, &m_VkSampler) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "Failed to create the bindless sampler.");
			return false;
		}
		return true;
	}

	bool VulkanDescriptors::Create(
		const VulkanContext& context,
		uint32 uFrameCount,
		const VulkanBuffer* const* ppFrameUniformBuffers)
	{
		if (IsValid())
		{
			LOG_ERROR(kLogTag, "VulkanDescriptors are already created.");
			return false;
		}
		if (uFrameCount == 0 || uFrameCount > k_nMaxDescriptorSetCount || ppFrameUniformBuffers == nullptr)
		{
			LOG_ERROR(kLogTag, "Invalid descriptor set creation arguments.");
			return false;
		}

		const VkDevice device = context.GetDevice();

		if (!CreateSharedSampler(context))
		{
			return false;
		}

		// Layout: the engine's resource set (see Graphics/ShaderBindings.h), which
		// HLSLCC assigns to every shader it compiles, so the numbers here and the
		// numbers in a compiled shader come from the same rule.
		// HLSL "Texture2D" maps to VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE and
		// "SamplerState" to VK_DESCRIPTOR_TYPE_SAMPLER, which is exactly what the
		// engine's shaders compile to.
		VkDescriptorSetLayoutBinding bindings[ShaderBindings::k_nBindingCount] = {};
		bindings[ShaderBindings::k_nCameraUniformBuffer].binding = ShaderBindings::k_nCameraUniformBuffer;
		bindings[ShaderBindings::k_nCameraUniformBuffer].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[ShaderBindings::k_nCameraUniformBuffer].descriptorCount = 1;
		bindings[ShaderBindings::k_nCameraUniformBuffer].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		bindings[ShaderBindings::k_nBindlessTextures].binding = ShaderBindings::k_nBindlessTextures;
		bindings[ShaderBindings::k_nBindlessTextures].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		bindings[ShaderBindings::k_nBindlessTextures].descriptorCount = k_nMaxBindlessTextureCount;
		bindings[ShaderBindings::k_nBindlessTextures].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		bindings[ShaderBindings::k_nBindlessSampler].binding = ShaderBindings::k_nBindlessSampler;
		bindings[ShaderBindings::k_nBindlessSampler].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
		bindings[ShaderBindings::k_nBindlessSampler].descriptorCount = 1;
		bindings[ShaderBindings::k_nBindlessSampler].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		// One flag entry per layout binding: binding 0 and 2 are single
		// descriptors, binding 1 is partially bound (only the slots the engine
		// actually created are written, and shaders only index those).
		VkDescriptorBindingFlags bindingFlags[ShaderBindings::k_nBindingCount] = {};
		bindingFlags[ShaderBindings::k_nBindlessTextures] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

		VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsCreateInfo = {};
		bindingFlagsCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		bindingFlagsCreateInfo.bindingCount = ShaderBindings::k_nBindingCount;
		bindingFlagsCreateInfo.pBindingFlags = bindingFlags;

		VkDescriptorSetLayoutCreateInfo layoutCreateInfo = {};
		layoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutCreateInfo.pNext = &bindingFlagsCreateInfo;
		layoutCreateInfo.bindingCount = ShaderBindings::k_nBindingCount;
		layoutCreateInfo.pBindings = bindings;

		if (vkCreateDescriptorSetLayout(device, &layoutCreateInfo, nullptr, &m_VkDescriptorSetLayout) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "Failed to create the bindless descriptor set layout.");
			return false;
		}

		// Pool: one set per frame in flight. Each bindless set carries the full
		// sampled-image array, so the pool limit must cover every slot.
		VkDescriptorPoolSize poolSizes[3] = {};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = uFrameCount;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		poolSizes[1].descriptorCount = uFrameCount * k_nMaxBindlessTextureCount;
		poolSizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;   // The engine's sampler binding.
		poolSizes[2].descriptorCount = uFrameCount;

		VkDescriptorPoolCreateInfo poolCreateInfo = {};
		poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolCreateInfo.maxSets = uFrameCount;
		poolCreateInfo.poolSizeCount = 3;
		poolCreateInfo.pPoolSizes = poolSizes;

		if (vkCreateDescriptorPool(device, &poolCreateInfo, nullptr, &m_VkDescriptorPool) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "Failed to create the bindless descriptor pool.");
			return false;
		}

		VkDescriptorSetLayout layouts[k_nMaxDescriptorSetCount] =
		{
			m_VkDescriptorSetLayout,
			m_VkDescriptorSetLayout,
		};

		VkDescriptorSetAllocateInfo allocateInfo = {};
		allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateInfo.descriptorPool = m_VkDescriptorPool;
		allocateInfo.descriptorSetCount = uFrameCount;
		allocateInfo.pSetLayouts = layouts;

		if (vkAllocateDescriptorSets(device, &allocateInfo, m_VkDescriptorSets) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "Failed to allocate the bindless descriptor sets.");
			return false;
		}
		m_nSetCount = uFrameCount;

		// Write the per-frame UBOs and the shared sampler. The sampled-image
		// array stays unwritten: the binding is partially bound, so a shader may
		// only index the slots WriteTextureSlot filled in.
		VkDescriptorImageInfo samplerInfo = {};
		samplerInfo.sampler = m_VkSampler;

		for (uint32 uFrameIndex = 0; uFrameIndex < uFrameCount; ++uFrameIndex)
		{
			VkDescriptorBufferInfo bufferInfo = {};
			bufferInfo.buffer = ppFrameUniformBuffers[uFrameIndex]->GetBuffer();
			bufferInfo.offset = 0;
			bufferInfo.range = ppFrameUniformBuffers[uFrameIndex]->GetByteSize();

			VkWriteDescriptorSet writes[2] = {};
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].dstSet = m_VkDescriptorSets[uFrameIndex];
			writes[0].dstBinding = ShaderBindings::k_nCameraUniformBuffer;
			writes[0].dstArrayElement = 0;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writes[0].descriptorCount = 1;
			writes[0].pBufferInfo = &bufferInfo;

			writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[1].dstSet = m_VkDescriptorSets[uFrameIndex];
			writes[1].dstBinding = ShaderBindings::k_nBindlessSampler;
			writes[1].dstArrayElement = 0;
			writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
			writes[1].descriptorCount = 1;
			writes[1].pImageInfo = &samplerInfo;

			vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
		}

		LOG_INFO(kLogTag, "Bindless descriptors ready ({} sampled-image slots).", k_nMaxBindlessTextureCount);
		return true;
	}

	bool VulkanDescriptors::WriteTextureSlot(
		const VulkanContext& context,
		uint32 uBindlessSlot,
		VkImageView textureView)
	{
		if (!IsValid() || m_nSetCount == 0)
		{
			LOG_ERROR(kLogTag, "Cannot write a texture slot before the descriptor sets exist.");
			return false;
		}
		if (uBindlessSlot >= k_nMaxBindlessTextureCount || textureView == VK_NULL_HANDLE)
		{
			LOG_ERROR(kLogTag, "Invalid bindless texture slot write.");
			return false;
		}

		const VkDevice device = context.GetDevice();

		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = textureView;

		VkWriteDescriptorSet writes[k_nMaxDescriptorSetCount] = {};
		for (uint32 uFrameIndex = 0; uFrameIndex < m_nSetCount; ++uFrameIndex)
		{
			writes[uFrameIndex].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[uFrameIndex].dstSet = m_VkDescriptorSets[uFrameIndex];
			writes[uFrameIndex].dstBinding = ShaderBindings::k_nBindlessTextures;
			writes[uFrameIndex].dstArrayElement = uBindlessSlot;
			writes[uFrameIndex].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			writes[uFrameIndex].descriptorCount = 1;
			writes[uFrameIndex].pImageInfo = &imageInfo;
		}

		vkUpdateDescriptorSets(device, m_nSetCount, writes, 0, nullptr);
		return true;
	}

	void VulkanDescriptors::Destroy(const VulkanContext& context)
	{
		const VkDevice device = context.GetDevice();

		if (m_VkSampler != VK_NULL_HANDLE) { vkDestroySampler(device, m_VkSampler, nullptr); m_VkSampler = VK_NULL_HANDLE; }
		if (m_VkDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device, m_VkDescriptorPool, nullptr); m_VkDescriptorPool = VK_NULL_HANDLE; }
		if (m_VkDescriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, m_VkDescriptorSetLayout, nullptr); m_VkDescriptorSetLayout = VK_NULL_HANDLE; }
		m_nSetCount = 0;
	}
}

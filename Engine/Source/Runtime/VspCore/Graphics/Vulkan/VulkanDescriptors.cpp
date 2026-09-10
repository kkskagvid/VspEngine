#include "RuntimePCH.h"

#include "Core/Logging/Log.h"
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

	bool VulkanDescriptors::Create(
		const VulkanContext& context,
		uint32 uFrameCount,
		const VulkanBuffer* const* ppFrameUniformBuffers,
		VkImageView textureView,
		VkSampler textureSampler)
	{
		if (IsValid())
		{
			LOG_ERROR(kLogTag, "VulkanDescriptors are already created.");
			return false;
		}
		if (uFrameCount == 0 || uFrameCount > k_nMaxDescriptorSetCount ||
			ppFrameUniformBuffers == nullptr || textureView == VK_NULL_HANDLE || textureSampler == VK_NULL_HANDLE)
		{
			LOG_ERROR(kLogTag, "Invalid descriptor set creation arguments.");
			return false;
		}

		const VkDevice device = context.GetDevice();

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
			LOG_ERROR(kLogTag, "Failed to create the bindless descriptor set layout.");
			return false;
		}

		// Pool: one set per frame in flight. Each bindless set carries a
		// 4096-slot sampled-image array, so the pool limit must cover every
		// slot.
		VkDescriptorPoolSize poolSizes[2] = {};
		poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSizes[0].descriptorCount = uFrameCount;
		poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[1].descriptorCount = uFrameCount * k_nMaxBindlessTextureCount;

		VkDescriptorPoolCreateInfo poolCreateInfo = {};
		poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolCreateInfo.maxSets = uFrameCount;
		poolCreateInfo.poolSizeCount = 2;
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

		// Write the per-frame UBOs + the texture into slot 0 of the bindless
		// array (partially bound: all other slots stay uninitialized).
		VkDescriptorImageInfo imageInfo = {};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = textureView;
		imageInfo.sampler = textureSampler;

		for (uint32 uFrameIndex = 0; uFrameIndex < uFrameCount; ++uFrameIndex)
		{
			VkDescriptorBufferInfo bufferInfo = {};
			bufferInfo.buffer = ppFrameUniformBuffers[uFrameIndex]->GetBuffer();
			bufferInfo.offset = 0;
			bufferInfo.range = ppFrameUniformBuffers[uFrameIndex]->GetByteSize();

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

	void VulkanDescriptors::Destroy(const VulkanContext& context)
	{
		const VkDevice device = context.GetDevice();

		if (m_VkDescriptorPool != VK_NULL_HANDLE) { vkDestroyDescriptorPool(device, m_VkDescriptorPool, nullptr); m_VkDescriptorPool = VK_NULL_HANDLE; }
		if (m_VkDescriptorSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(device, m_VkDescriptorSetLayout, nullptr); m_VkDescriptorSetLayout = VK_NULL_HANDLE; }
		m_nSetCount = 0;
	}
}

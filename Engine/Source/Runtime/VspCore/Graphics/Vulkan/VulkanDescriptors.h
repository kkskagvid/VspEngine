#pragma once

#include <vulkan/vulkan.h>

#include "Core/Core.h"

namespace Vsp
{
	class VulkanContext;
	class VulkanBuffer;

	// -------------------------------------------------------------------------
	// VulkanDescriptors
	// -------------------------------------------------------------------------
	// Functional unit owning the bindless descriptor machinery: the set
	// layout (binding 0 = per-frame camera UBO, binding 1 = the 4096-slot
	// combined-image-sampler array), the descriptor pool and one descriptor
	// set per frame in flight. Create() also performs the initial writes
	// (per-frame uniform buffers + the texture in array slot 0; the rest of
	// the array stays partially bound). Bindless is the only supported
	// implementation - there is no fallback path.
	// Every function logs its own errors through the Log module and never
	// throws.
	// -------------------------------------------------------------------------
	class VulkanDescriptors
	{
	public:
		static constexpr uint32 k_nMaxBindlessTextureCount = 4096;
		static constexpr uint32 k_nMaxDescriptorSetCount = 2;   // Frames in flight.

		~VulkanDescriptors();

		// Creates the layout, the pool and uFrameCount sets (max
		// k_nMaxDescriptorSetCount) and writes the initial descriptors.
		// ppFrameUniformBuffers addresses uFrameCount per-frame uniform
		// buffer pointers (one per descriptor set).
		bool Create(
			const VulkanContext& context,
			uint32 uFrameCount,
			const VulkanBuffer* const* ppFrameUniformBuffers,
			VkImageView textureView,
			VkSampler textureSampler);

		void Destroy(const VulkanContext& context);

		bool IsValid() const { return m_VkDescriptorSetLayout != VK_NULL_HANDLE; }
		VkDescriptorSetLayout GetLayout() const { return m_VkDescriptorSetLayout; }
		VkDescriptorSet GetSet(uint32 uFrameIndex) const { return m_VkDescriptorSets[uFrameIndex]; }

	private:
		VkDescriptorSetLayout m_VkDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_VkDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet m_VkDescriptorSets[k_nMaxDescriptorSetCount] = {};
		uint32 m_nSetCount = 0;
	};
}

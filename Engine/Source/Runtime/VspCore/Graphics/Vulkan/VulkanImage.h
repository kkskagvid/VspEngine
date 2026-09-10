#pragma once

#include <vulkan/vulkan.h>

#include "Core/Core.h"

namespace Vsp
{
	class VulkanContext;

	// -------------------------------------------------------------------------
	// VulkanImage
	// -------------------------------------------------------------------------
	// Functional unit owning one 2D texture: the device image, its bound
	// memory, the image view and the sampler. Create() uploads RGBA8 pixel
	// data through a staging buffer and performs the layout transitions
	// (UNDEFINED -> TRANSFER_DST -> SHADER_READ_ONLY). The unit is the split
	// home of everything the renderer previously did inline for its demo
	// texture.
	// Every function logs its own errors through the Log module and never
	// throws.
	// -------------------------------------------------------------------------
	class VulkanImage
	{
	public:
		~VulkanImage();

		// Creates the image, view and sampler from the given RGBA8 pixels
		// (uWidth * uHeight * 4 bytes). Returns false on failure.
		bool Create(
			const VulkanContext& context,
			uint32 uWidth,
			uint32 uHeight,
			const void* pPixelDataRgba8);

		void Destroy(const VulkanContext& context);

		bool IsValid() const { return m_VkImage != VK_NULL_HANDLE; }
		VkImage GetImage() const { return m_VkImage; }
		VkImageView GetView() const { return m_VkImageView; }
		VkSampler GetSampler() const { return m_VkSampler; }

	private:
		// One layout transition executed through a one-time command buffer.
		static bool TransitionImageLayout(
			const VulkanContext& context,
			VkImage image,
			VkImageLayout eOldLayout,
			VkImageLayout eNewLayout);

		VkImage m_VkImage = VK_NULL_HANDLE;
		VkDeviceMemory m_VkImageMemory = VK_NULL_HANDLE;
		VkImageView m_VkImageView = VK_NULL_HANDLE;
		VkSampler m_VkSampler = VK_NULL_HANDLE;
	};
}

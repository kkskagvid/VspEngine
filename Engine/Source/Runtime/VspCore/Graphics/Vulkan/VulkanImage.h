#pragma once

#include <vulkan/vulkan.h>

#include "Core/Core.h"

namespace Vsp
{
	class VulkanContext;

	// -------------------------------------------------------------------------
	// VulkanImage
	// -------------------------------------------------------------------------
	// Functional unit owning one 2D texture's data: the device image, its bound
	// memory and the image view. Create() uploads RGBA8 pixel data through a
	// staging buffer and performs the layout transitions (UNDEFINED ->
	// TRANSFER_DST -> SHADER_READ_ONLY).
	//
	// The SAMPLER is deliberately not part of this unit: the engine is bindless,
	// so every texture is read through the one sampler
	// VulkanDescriptors owns (see its binding 2), which is also how the HLSL
	// shaders compile - a separate SamplerState instead of a combined image
	// sampler.
	// Every function logs its own errors through the Log module and never
	// throws.
	// -------------------------------------------------------------------------
	class VulkanImage
	{
	public:
		VulkanImage() = default;
		~VulkanImage();

		// An image owns its handles, so it moves but never copies; moving
		// leaves the source empty (Destroy() on it then does nothing).
		VulkanImage(const VulkanImage&) = delete;
		VulkanImage& operator=(const VulkanImage&) = delete;
		VulkanImage(VulkanImage&& Other) noexcept;
		VulkanImage& operator=(VulkanImage&& Other) noexcept;

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
	};
}

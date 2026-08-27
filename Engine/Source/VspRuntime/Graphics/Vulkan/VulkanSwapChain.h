#pragma once

#include <vulkan/vulkan.h>

#include "Core/Core.h"
#include "Core/String/VspString.h"
#include "Core/Templates/ArrayList.h"
#include "Graphics/Vulkan/VulkanRHI.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// VulkanSwapChain
	// -------------------------------------------------------------------------
	// Owns the swapchain, its image views, the framebuffers and the single
	// render pass used by the 2D renderer. Recreate() rebuilds everything
	// (resize / minimize handling). Acquire/SubmitAndPresent implement the
	// standard semaphore handshake around one submitted command buffer.
	// -------------------------------------------------------------------------
	class VulkanSwapChain
	{
	public:
		~VulkanSwapChain();

		bool Initialize(
			VulkanContext& context,
			uint32_t uPreferredWidth,
			uint32_t uPreferredHeight,
			VspString& outErrorText);

		void Destroy();

		// Rebuilds the swapchain for the new size. Returns false on hard failure.
		bool Recreate(uint32_t uWidth, uint32_t uHeight, VspString& outErrorText);

		VkSwapchainKHR GetSwapchain() const { return m_VkSwapchain; }
		VkRenderPass GetRenderPass() const { return m_VkRenderPass; }
		VkFramebuffer GetFramebuffer(uint32_t uImageIndex) const { return m_Framebuffers.At(uImageIndex); }
		VkImage GetImage(uint32_t uImageIndex) const { return m_SwapChainImages.At(uImageIndex); }
		VkFormat GetImageFormat() const { return m_eImageFormat; }
		VkExtent2D GetExtent() const { return m_Extent; }
		uint32_t GetImageCount() const { return static_cast<uint32_t>(m_SwapChainImageViews.GetSize()); }
		uint32_t GetCurrentImageIndex() const { return m_nCurrentImageIndex; }

		// Acquires the next image; false when out-of-date (caller should Recreate).
		bool AcquireNextImage(VkSemaphore imageAvailableSemaphore, VspString& outErrorText);

		// Submits the command buffer (signaling the caller's in-flight fence)
		// and presents the acquired image. Returns false for fatal errors;
		// out-of-date results are reported through outIsOutOfDate so the
		// caller can recreate the swapchain.
		bool SubmitAndPresent(
			VkCommandBuffer commandBuffer,
			VkSemaphore waitSemaphore,
			VkSemaphore signalSemaphore,
			VkFence inFlightFence,
			bool& outIsOutOfDate,
			VspString& outErrorText);

		void WaitIdle() const;

	private:
		bool CreateSwapChain(VspString& outErrorText);
		bool CreateImageViews(VspString& outErrorText);
		bool CreateRenderPass(VspString& outErrorText);
		bool CreateFramebuffers(VspString& outErrorText);
		void DestroySwapChainObjects();

		static VkSurfaceFormatKHR ChooseSurfaceFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
		static VkPresentModeKHR ChoosePresentMode(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
		static VkExtent2D ChooseExtent(
			const VkSurfaceCapabilitiesKHR& capabilities,
			uint32_t uPreferredWidth,
			uint32_t uPreferredHeight);

		VulkanContext* m_pContext = nullptr;

		VkSwapchainKHR m_VkSwapchain = VK_NULL_HANDLE;
		VkFormat m_eImageFormat = VK_FORMAT_UNDEFINED;
		VkExtent2D m_Extent = {};
		VkRenderPass m_VkRenderPass = VK_NULL_HANDLE;

		ArrayList<VkImage> m_SwapChainImages;
		ArrayList<VkImageView> m_SwapChainImageViews;
		ArrayList<VkFramebuffer> m_Framebuffers;

		uint32_t m_nCurrentImageIndex = 0;
	};
}

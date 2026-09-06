#pragma once

#include <vulkan/vulkan.h>

#include "Core/Core.h"
#include "Core/Templates/ArrayList.h"
#include "Graphics/Vulkan/VulkanRHI.h"

namespace Vsp
{
	// Result of acquiring the next presentable image.
	enum class SwapChainAcquireResult : uint32
	{
		Success = 0,
		OutOfDate = 1,   // Surface size changed; the caller should recreate.
		Failed = 2,      // Hard failure (details already logged).
	};

	// -------------------------------------------------------------------------
	// VulkanSwapChain
	// -------------------------------------------------------------------------
	// Owns the swapchain, its image views, the framebuffers and the single
	// render pass used by the 2D renderer. Recreate() rebuilds everything
	// (resize / minimize handling). Acquire/SubmitAndPresent implement the
	// standard semaphore handshake around one submitted command buffer.
	// All errors are logged through the Log module; nothing throws.
	// -------------------------------------------------------------------------
	class VulkanSwapChain
	{
	public:
		~VulkanSwapChain();

		bool Initialize(
			VulkanContext& context,
			uint32 uPreferredWidth,
			uint32 uPreferredHeight);

		void Destroy();

		// Rebuilds the swapchain for the new size. Returns false on hard failure.
		bool Recreate(uint32 uWidth, uint32 uHeight);

		VkSwapchainKHR GetSwapchain() const { return m_VkSwapchain; }
		VkRenderPass GetRenderPass() const { return m_VkRenderPass; }
		VkFramebuffer GetFramebuffer(uint32 uImageIndex) const { return m_Framebuffers.At(uImageIndex); }
		VkImage GetImage(uint32 uImageIndex) const { return m_SwapChainImages.At(uImageIndex); }
		VkFormat GetImageFormat() const { return m_eImageFormat; }
		VkExtent2D GetExtent() const { return m_Extent; }
		uint32 GetImageCount() const { return static_cast<uint32>(m_SwapChainImageViews.GetSize()); }
		uint32 GetCurrentImageIndex() const { return m_nCurrentImageIndex; }

		// Acquires the next image; OutOfDate means the caller should recreate.
		SwapChainAcquireResult AcquireNextImage(VkSemaphore imageAvailableSemaphore);

		// Submits the command buffer (signaling the caller's in-flight fence)
		// and presents the acquired image. Returns false for fatal errors;
		// out-of-date results are reported through outIsOutOfDate so the
		// caller can recreate the swapchain.
		bool SubmitAndPresent(
			VkCommandBuffer commandBuffer,
			VkSemaphore waitSemaphore,
			VkSemaphore signalSemaphore,
			VkFence inFlightFence,
			bool& outIsOutOfDate);

		void WaitIdle() const;

	private:
		bool CreateSwapChain();
		bool CreateImageViews();
		bool CreateRenderPass();
		bool CreateFramebuffers();
		void DestroySwapChainObjects();

		static VkSurfaceFormatKHR ChooseSurfaceFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
		static VkPresentModeKHR ChoosePresentMode(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
		static VkExtent2D ChooseExtent(
			const VkSurfaceCapabilitiesKHR& capabilities,
			uint32 uPreferredWidth,
			uint32 uPreferredHeight);

		VulkanContext* m_pContext = nullptr;

		VkSwapchainKHR m_VkSwapchain = VK_NULL_HANDLE;
		VkFormat m_eImageFormat = VK_FORMAT_UNDEFINED;
		VkExtent2D m_Extent = {};
		VkRenderPass m_VkRenderPass = VK_NULL_HANDLE;

		ArrayList<VkImage> m_SwapChainImages;
		ArrayList<VkImageView> m_SwapChainImageViews;
		ArrayList<VkFramebuffer> m_Framebuffers;

		uint32 m_nCurrentImageIndex = 0;
	};
}

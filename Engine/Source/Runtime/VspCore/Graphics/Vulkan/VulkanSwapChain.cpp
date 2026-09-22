#include "RuntimePCH.h"

#include <vector>

#include "Core/Logging/Log.h"
#include "Graphics/Vulkan/VulkanSwapChain.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "VulkanSwapChain";

	VulkanSwapChain::~VulkanSwapChain()
	{
		Destroy();
	}

	bool VulkanSwapChain::Initialize(
		VulkanContext& context,
		uint32 uPreferredWidth,
		uint32 uPreferredHeight)
	{
		m_pContext = &context;
		m_Extent.width = uPreferredWidth;
		m_Extent.height = uPreferredHeight;

		if (!CreateSwapChain()) return false;
		if (!CreateImageViews()) return false;

		// The depth format is part of the render pass, so it is chosen before
		// the pass is built; the images that use it come after.
		m_eDepthFormat = ChooseDepthFormat();

		if (!CreateRenderPass()) return false;
		if (!CreateDepthResources()) return false;
		if (!CreateFramebuffers()) return false;
		return true;
	}

	void VulkanSwapChain::Destroy()
	{
		if (m_pContext == nullptr)
		{
			return;
		}

		WaitIdle();
		DestroySwapChainObjects();

		if (m_VkRenderPass != VK_NULL_HANDLE)
		{
			vkDestroyRenderPass(m_pContext->GetDevice(), m_VkRenderPass, nullptr);
			m_VkRenderPass = VK_NULL_HANDLE;
		}
	}

	bool VulkanSwapChain::Recreate(uint32 uWidth, uint32 uHeight)
	{
		if (m_pContext == nullptr)
		{
			LOG_ERROR(kLogTag, "Cannot recreate the swapchain before initialization.");
			return false;
		}

		m_Extent.width = uWidth;
		m_Extent.height = uHeight;

		WaitIdle();
		DestroySwapChainObjects();

		if (!CreateSwapChain()) return false;
		if (!CreateImageViews()) return false;

		// A resize keeps the depth format and rebuilds the images for the new
		// extent, so the render pass still matches what it renders into.
		if (!CreateDepthResources()) return false;
		if (!CreateFramebuffers()) return false;
		return true;
	}

	void VulkanSwapChain::WaitIdle() const
	{
		if (m_pContext != nullptr && m_pContext->IsInitialized())
		{
			vkDeviceWaitIdle(m_pContext->GetDevice());
		}
	}

	// -------------------------------------------------------------------------
	// Creation
	// -------------------------------------------------------------------------

	bool VulkanSwapChain::CreateSwapChain()
	{
		const VkPhysicalDevice physicalDevice = m_pContext->GetPhysicalDevice();
		const VkSurfaceKHR surface = m_pContext->GetSurface();
		const VkDevice device = m_pContext->GetDevice();
		const uint32 nQueueFamilyIndex = m_pContext->GetGraphicsQueueFamilyIndex();

		VkSurfaceCapabilitiesKHR capabilities = {};
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

		LOG_INFO(kLogTag, "Surface: currentTransform={}, currentExtent={}x{}, minImageCount={}.",
			static_cast<uint32>(capabilities.currentTransform),
			capabilities.currentExtent.width, capabilities.currentExtent.height,
			capabilities.minImageCount);

		VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(physicalDevice, surface);
		VkPresentModeKHR presentMode = ChoosePresentMode(physicalDevice, surface);
		VkExtent2D extent = ChooseExtent(capabilities, m_Extent.width, m_Extent.height);

		uint32 nImageCount = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount > 0 && nImageCount > capabilities.maxImageCount)
		{
			nImageCount = capabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		createInfo.minImageCount = nImageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 1;
		createInfo.pQueueFamilyIndices = &nQueueFamilyIndex;
		createInfo.preTransform = capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		const VkResult eResult = vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_VkSwapchain);
		if (eResult != VK_SUCCESS || m_VkSwapchain == VK_NULL_HANDLE)
		{
			LOG_ERROR(kLogTag, "vkCreateSwapchainKHR failed (VkResult {}).", static_cast<int32>(eResult));
			return false;
		}

		m_eImageFormat = surfaceFormat.format;
		m_Extent = extent;

		uint32 nActualImageCount = 0;
		vkGetSwapchainImagesKHR(device, m_VkSwapchain, &nActualImageCount, nullptr);
		std::vector<VkImage> images(nActualImageCount);
		vkGetSwapchainImagesKHR(device, m_VkSwapchain, &nActualImageCount, images.data());
		m_SwapChainImages.Reserve(nActualImageCount);
		for (VkImage image : images)
		{
			m_SwapChainImages.Add(image);
		}

		LOG_INFO(kLogTag, "Swapchain created: {} images, {}x{}.", nActualImageCount, extent.width, extent.height);
		return true;
	}

	bool VulkanSwapChain::CreateImageViews()
	{
		const VkDevice device = m_pContext->GetDevice();
		m_SwapChainImageViews.Clear();
		m_SwapChainImageViews.Reserve(m_SwapChainImages.GetSize());

		for (size_t nIndex = 0; nIndex < m_SwapChainImages.GetSize(); ++nIndex)
		{
			VkImageViewCreateInfo viewCreateInfo = {};
			viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewCreateInfo.image = m_SwapChainImages[nIndex];
			viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewCreateInfo.format = m_eImageFormat;
			viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewCreateInfo.subresourceRange.baseMipLevel = 0;
			viewCreateInfo.subresourceRange.levelCount = 1;
			viewCreateInfo.subresourceRange.baseArrayLayer = 0;
			viewCreateInfo.subresourceRange.layerCount = 1;

			VkImageView imageView = VK_NULL_HANDLE;
			if (vkCreateImageView(device, &viewCreateInfo, nullptr, &imageView) != VK_SUCCESS)
			{
				LOG_ERROR(kLogTag, "vkCreateImageView failed for swapchain image {}.", nIndex);
				return false;
			}
			m_SwapChainImageViews.Add(imageView);
		}
		return true;
	}

	bool VulkanSwapChain::CreateRenderPass()
	{
		VkAttachmentDescription colorAttachment = {};
		colorAttachment.format = m_eImageFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorAttachmentReference = {};
		colorAttachmentReference.attachment = 0;
		colorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		// The depth attachment every pipeline may test against. It is cleared
		// together with the color target, so a frame always starts with a clean
		// depth buffer.
		VkAttachmentDescription depthAttachment = {};
		depthAttachment.format = m_eDepthFormat;
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthAttachmentReference = {};
		depthAttachmentReference.attachment = 1;
		depthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentReference;
		subpass.pDepthStencilAttachment = &depthAttachmentReference;

		VkSubpassDependency dependency = {};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		const VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };

		VkRenderPassCreateInfo renderPassCreateInfo = {};
		renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassCreateInfo.attachmentCount = 2;
		renderPassCreateInfo.pAttachments = attachments;
		renderPassCreateInfo.subpassCount = 1;
		renderPassCreateInfo.pSubpasses = &subpass;
		renderPassCreateInfo.dependencyCount = 1;
		renderPassCreateInfo.pDependencies = &dependency;

		const VkResult eResult = vkCreateRenderPass(m_pContext->GetDevice(), &renderPassCreateInfo, nullptr, &m_VkRenderPass);
		if (eResult != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreateRenderPass failed (VkResult {}).", static_cast<int32>(eResult));
			return false;
		}
		return true;
	}

	VkFormat VulkanSwapChain::ChooseDepthFormat() const
	{
		// Depth-stencil first (it is the better supported format everywhere),
		// then plain depth.
		const VkFormat candidates[] =
		{
			VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D24_UNORM_S8_UINT,
		};

		for (const VkFormat eFormat : candidates)
		{
			VkFormatProperties properties = {};
			vkGetPhysicalDeviceFormatProperties(m_pContext->GetPhysicalDevice(), eFormat, &properties);
			if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
			{
				return eFormat;
			}
		}

		return VK_FORMAT_D32_SFLOAT;
	}

	bool VulkanSwapChain::CreateDepthResources()
	{
		const VkDevice device = m_pContext->GetDevice();

		m_DepthImages.Clear();
		m_DepthImageMemories.Clear();
		m_DepthImageViews.Clear();

		for (size_t nIndex = 0; nIndex < m_SwapChainImages.GetSize(); ++nIndex)
		{
			VkImageCreateInfo imageCreateInfo = {};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = m_eDepthFormat;
			imageCreateInfo.extent.width = m_Extent.width;
			imageCreateInfo.extent.height = m_Extent.height;
			imageCreateInfo.extent.depth = 1;
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			VkImage depthImage = VK_NULL_HANDLE;
			if (vkCreateImage(device, &imageCreateInfo, nullptr, &depthImage) != VK_SUCCESS)
			{
				LOG_ERROR(kLogTag, "vkCreateImage failed for depth image {}.", nIndex);
				return false;
			}

			VkMemoryRequirements memoryRequirements = {};
			vkGetImageMemoryRequirements(device, depthImage, &memoryRequirements);

			VkMemoryAllocateInfo allocateInfo = {};
			allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocateInfo.allocationSize = memoryRequirements.size;
			allocateInfo.memoryTypeIndex = m_pContext->FindMemoryTypeIndex(
				memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VkDeviceMemory depthMemory = VK_NULL_HANDLE;
			if (vkAllocateMemory(device, &allocateInfo, nullptr, &depthMemory) != VK_SUCCESS)
			{
				LOG_ERROR(kLogTag, "vkAllocateMemory failed for depth image {}.", nIndex);
				vkDestroyImage(device, depthImage, nullptr);
				return false;
			}
			vkBindImageMemory(device, depthImage, depthMemory, 0);

			VkImageViewCreateInfo viewCreateInfo = {};
			viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewCreateInfo.image = depthImage;
			viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewCreateInfo.format = m_eDepthFormat;
			viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			viewCreateInfo.subresourceRange.baseMipLevel = 0;
			viewCreateInfo.subresourceRange.levelCount = 1;
			viewCreateInfo.subresourceRange.baseArrayLayer = 0;
			viewCreateInfo.subresourceRange.layerCount = 1;

			VkImageView depthImageView = VK_NULL_HANDLE;
			if (vkCreateImageView(device, &viewCreateInfo, nullptr, &depthImageView) != VK_SUCCESS)
			{
				LOG_ERROR(kLogTag, "vkCreateImageView failed for depth image {}.", nIndex);
				vkDestroyImage(device, depthImage, nullptr);
				vkFreeMemory(device, depthMemory, nullptr);
				return false;
			}

			m_DepthImages.Add(depthImage);
			m_DepthImageMemories.Add(depthMemory);
			m_DepthImageViews.Add(depthImageView);
		}

		return true;
	}

	bool VulkanSwapChain::CreateFramebuffers()
	{
		const VkDevice device = m_pContext->GetDevice();
		m_Framebuffers.Clear();
		m_Framebuffers.Reserve(m_SwapChainImageViews.GetSize());

		for (size_t nIndex = 0; nIndex < m_SwapChainImageViews.GetSize(); ++nIndex)
		{
			// The framebuffer carries the color target and the depth image that
			// belongs to this swapchain image.
			VkImageView attachments[] = { m_SwapChainImageViews[nIndex], m_DepthImageViews[nIndex] };

			VkFramebufferCreateInfo framebufferCreateInfo = {};
			framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferCreateInfo.renderPass = m_VkRenderPass;
			framebufferCreateInfo.attachmentCount = 2;
			framebufferCreateInfo.pAttachments = attachments;
			framebufferCreateInfo.width = m_Extent.width;
			framebufferCreateInfo.height = m_Extent.height;
			framebufferCreateInfo.layers = 1;

			VkFramebuffer framebuffer = VK_NULL_HANDLE;
			if (vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &framebuffer) != VK_SUCCESS)
			{
				LOG_ERROR(kLogTag, "vkCreateFramebuffer failed for image {}.", nIndex);
				return false;
			}
			m_Framebuffers.Add(framebuffer);
		}
		return true;
	}

	void VulkanSwapChain::DestroySwapChainObjects()
	{
		const VkDevice device = m_pContext->GetDevice();

		for (size_t nIndex = 0; nIndex < m_Framebuffers.GetSize(); ++nIndex)
		{
			vkDestroyFramebuffer(device, m_Framebuffers[nIndex], nullptr);
		}
		m_Framebuffers.Clear();

		for (size_t nIndex = 0; nIndex < m_DepthImageViews.GetSize(); ++nIndex)
		{
			vkDestroyImageView(device, m_DepthImageViews[nIndex], nullptr);
		}
		m_DepthImageViews.Clear();

		for (size_t nIndex = 0; nIndex < m_DepthImages.GetSize(); ++nIndex)
		{
			vkDestroyImage(device, m_DepthImages[nIndex], nullptr);
		}
		m_DepthImages.Clear();

		for (size_t nIndex = 0; nIndex < m_DepthImageMemories.GetSize(); ++nIndex)
		{
			vkFreeMemory(device, m_DepthImageMemories[nIndex], nullptr);
		}
		m_DepthImageMemories.Clear();

		for (size_t nIndex = 0; nIndex < m_SwapChainImageViews.GetSize(); ++nIndex)
		{
			vkDestroyImageView(device, m_SwapChainImageViews[nIndex], nullptr);
		}
		m_SwapChainImageViews.Clear();
		m_SwapChainImages.Clear();

		if (m_VkSwapchain != VK_NULL_HANDLE)
		{
			vkDestroySwapchainKHR(device, m_VkSwapchain, nullptr);
			m_VkSwapchain = VK_NULL_HANDLE;
		}
	}

	// -------------------------------------------------------------------------
	// Presentation
	// -------------------------------------------------------------------------

	SwapChainAcquireResult VulkanSwapChain::AcquireNextImage(VkSemaphore imageAvailableSemaphore)
	{
		const VkResult eResult = vkAcquireNextImageKHR(
			m_pContext->GetDevice(),
			m_VkSwapchain,
			UINT64_MAX,
			imageAvailableSemaphore,
			VK_NULL_HANDLE,
			&m_nCurrentImageIndex);

		if (eResult == VK_ERROR_OUT_OF_DATE_KHR || eResult == VK_SUBOPTIMAL_KHR)
		{
			return SwapChainAcquireResult::OutOfDate;
		}

		if (eResult != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkAcquireNextImageKHR failed (VkResult {}).", static_cast<int32>(eResult));
			return SwapChainAcquireResult::Failed;
		}
		return SwapChainAcquireResult::Success;
	}

	bool VulkanSwapChain::SubmitAndPresent(
		VkCommandBuffer commandBuffer,
		VkSemaphore waitSemaphore,
		VkSemaphore signalSemaphore,
		VkFence inFlightFence,
		bool& outIsOutOfDate)
	{
		outIsOutOfDate = false;

		const VkPipelineStageFlags k_waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &waitSemaphore;
		submitInfo.pWaitDstStageMask = &k_waitStage;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &signalSemaphore;

		// The frame fence is signaled when this submit completes; the caller
		// waits on it before reusing the frame's command buffer.
		const VkResult eSubmitResult = vkQueueSubmit(m_pContext->GetGraphicsQueue(), 1, &submitInfo, inFlightFence);
		if (eSubmitResult != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkQueueSubmit failed (VkResult {}).", static_cast<int32>(eSubmitResult));
			return false;
		}

		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &signalSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &m_VkSwapchain;
		presentInfo.pImageIndices = &m_nCurrentImageIndex;

		const VkResult ePresentResult = vkQueuePresentKHR(m_pContext->GetGraphicsQueue(), &presentInfo);
		if (ePresentResult == VK_ERROR_OUT_OF_DATE_KHR || ePresentResult == VK_SUBOPTIMAL_KHR)
		{
			outIsOutOfDate = true;
			return true;   // Frame completed; swapchain just needs a rebuild.
		}

		if (ePresentResult != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkQueuePresentKHR failed (VkResult {}).", static_cast<int32>(ePresentResult));
			return false;
		}
		return true;
	}

	// -------------------------------------------------------------------------
	// Format / mode / extent selection
	// -------------------------------------------------------------------------

	VkSurfaceFormatKHR VulkanSwapChain::ChooseSurfaceFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
	{
		uint32 nFormatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &nFormatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(nFormatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &nFormatCount, formats.data());

		for (const VkSurfaceFormatKHR& format : formats)
		{
			if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
				format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				return format;
			}
		}

		// A surface always exposes at least one format.
		return formats[0];
	}

	VkPresentModeKHR VulkanSwapChain::ChoosePresentMode(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
	{
		uint32 nModeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &nModeCount, nullptr);
		std::vector<VkPresentModeKHR> modes(nModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &nModeCount, modes.data());

		for (VkPresentModeKHR mode : modes)
		{
			if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				return mode;
			}
		}
		return VK_PRESENT_MODE_FIFO_KHR;   // Always available.
	}

	VkExtent2D VulkanSwapChain::ChooseExtent(
		const VkSurfaceCapabilitiesKHR& capabilities,
		uint32 uPreferredWidth,
		uint32 uPreferredHeight)
	{
		if (capabilities.currentExtent.width != UINT32_MAX)
		{
			return capabilities.currentExtent;
		}

		VkExtent2D extent = { uPreferredWidth, uPreferredHeight };
		if (extent.width < capabilities.minImageExtent.width) extent.width = capabilities.minImageExtent.width;
		if (extent.width > capabilities.maxImageExtent.width) extent.width = capabilities.maxImageExtent.width;
		if (extent.height < capabilities.minImageExtent.height) extent.height = capabilities.minImageExtent.height;
		if (extent.height > capabilities.maxImageExtent.height) extent.height = capabilities.maxImageExtent.height;
		return extent;
	}
}

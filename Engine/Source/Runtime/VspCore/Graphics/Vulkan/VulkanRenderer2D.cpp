#include "RuntimePCH.h"

#include <cstdio>
#include <cstring>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Core/Logging/Log.h"
#include "Graphics/Vulkan/VulkanRenderer2D.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "VulkanRenderer2D";

	// The acceptance triangle: one vertex per corner, distinct vertex colors.
	static constexpr Vertex2D k_sTriangleVertices[] =
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
		// 1. Context facade (instance + surface + device), swapchain, frames.
		if (!m_Context.Initialize("Vsp Engine", pNativeWindowHandle))
		{
			// Device below Vulkan 1.3 lands here: the context has already
			// logged the "Unsupported device" details through the Log module.
			return false;
		}

		if (!m_SwapChain.Initialize(m_Context, 1280, 720)) return false;
		if (!CreateFrameResources()) return false;
		if (!CreateTriangleGeometry()) return false;

		// 2. Demo texture (white 8x8 - keeps the acceptance colors exact
		//    while the bindless array sampling is exercised).
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
		if (!m_DemoTexture.Create(m_Context, k_nTextureWidth, k_nTextureHeight, texturePixels)) return false;

		// 3. Descriptor set layout first, then the pipeline that references it.
		//    Bindless is the only supported implementation.
		const VulkanBuffer* ppFrameUniformBuffers[k_nMaxFramesInFlight] =
		{
			&m_Frames[0].CameraUniformBuffer,
			&m_Frames[1].CameraUniformBuffer,
		};
		if (!m_BindlessDescriptors.Create(
			m_Context,
			k_nMaxFramesInFlight,
			ppFrameUniformBuffers,
			m_DemoTexture.GetView(),
			m_DemoTexture.GetSampler()))
		{
			return false;
		}
		if (!m_TrianglePipeline.Create(
			m_Context,
			m_SwapChain.GetRenderPass(),
			m_BindlessDescriptors.GetLayout()))
		{
			return false;
		}

		m_bIsInitialized = true;
		LOG_INFO(kLogTag, "Renderer ready (feature path: Vulkan 1.3 bindless).");
		return true;
	}

	void VulkanRenderer2D::Shutdown()
	{
		if (m_Context.IsInitialized())
		{
			m_Context.WaitIdle();
		}

		DestroyFrameResources();
		DestroyTriangleGeometry();
		m_DemoTexture.Destroy(m_Context);
		m_BindlessDescriptors.Destroy(m_Context);
		m_TrianglePipeline.Destroy(m_Context);

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
			if (!frame.CameraUniformBuffer.Allocate(
				m_Context,
				sizeof(CameraUniformData),
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
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
			frame.CameraUniformBuffer.Destroy(m_Context);
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

		if (!m_TriangleVertexBuffer.Allocate(
			m_Context,
			k_nBufferByteSize,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
		{
			vkDestroyBuffer(m_Context.GetDevice(), stagingBuffer, nullptr);
			vkFreeMemory(m_Context.GetDevice(), stagingBufferMemory, nullptr);
			return false;
		}

		// Copy staging -> device local through a one-time command buffer.
		VkCommandBuffer commandBuffer = m_Context.BeginOneTimeCommandBuffer();
		if (commandBuffer == VK_NULL_HANDLE)
		{
			vkDestroyBuffer(m_Context.GetDevice(), stagingBuffer, nullptr);
			vkFreeMemory(m_Context.GetDevice(), stagingBufferMemory, nullptr);
			return false;
		}

		VkBufferCopy copyRegion = {};
		copyRegion.size = k_nBufferByteSize;
		vkCmdCopyBuffer(commandBuffer, stagingBuffer, m_TriangleVertexBuffer.GetBuffer(), 1, &copyRegion);

		const bool bSuccess = m_Context.EndOneTimeCommandBuffer(commandBuffer);

		vkDestroyBuffer(m_Context.GetDevice(), stagingBuffer, nullptr);
		vkFreeMemory(m_Context.GetDevice(), stagingBufferMemory, nullptr);
		return bSuccess;
	}

	void VulkanRenderer2D::DestroyTriangleGeometry()
	{
		m_TriangleVertexBuffer.Destroy(m_Context);
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
		// Draw positions arrive per draw as vertex push-constant offsets, so
		// the camera matrix is the projection alone.
		const VkExtent2D extent = m_SwapChain.GetExtent();
		const float fAspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);

		const glm::mat4 projectionMatrix = glm::ortho(-fAspectRatio, fAspectRatio, 1.0f, -1.0f, -1.0f, 1.0f);

		CameraUniformData uniformData;
		memcpy(uniformData.m4ViewProjection, &projectionMatrix[0][0], sizeof(uniformData.m4ViewProjection));

		m_Frames[uFrameIndex].CameraUniformBuffer.WriteData(m_Context, &uniformData, sizeof(CameraUniformData));
	}

	void VulkanRenderer2D::BuildPushConstants(
		const RenderCore::TriangleDrawCommand& drawCommand,
		PushConstants& outPushConstants) const
	{
		outPushConstants = {};
		outPushConstants.fPositionOffsetX = drawCommand.fPositionX;
		outPushConstants.fPositionOffsetY = drawCommand.fPositionY;
		outPushConstants.uTextureIndex = 0;   // Demo texture lives in slot 0.
		outPushConstants.nColorMode = drawCommand.nColorMode;

		switch (drawCommand.nColorMode)
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
		// The frame commands were submitted by the managed render flow
		// (RenderCore). A frame that was not opened/closed in order falls
		// back to the default frame so the window always shows something.
		const RenderCore& renderCore = RenderCore::Get();
		const bool bHasValidFrame = renderCore.IsFrameValid();

		RenderCore::FrameClearColor clearColor;
		if (bHasValidFrame)
		{
			clearColor = renderCore.GetClearColor();
		}

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
		clearValue.color = { { clearColor.fColorR, clearColor.fColorG, clearColor.fColorB, clearColor.fColorA } };
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

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_TrianglePipeline.GetPipeline());

		VkDescriptorSet descriptorSet = m_BindlessDescriptors.GetSet(m_nCurrentFrameIndex);
		vkCmdBindDescriptorSets(
			commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			m_TrianglePipeline.GetLayout(),
			0,
			1,
			&descriptorSet,
			0,
			nullptr);

		const VkDeviceSize k_nVertexBufferOffset = 0;
		VkBuffer vertexBuffer = m_TriangleVertexBuffer.GetBuffer();
		vkCmdBindVertexBuffers(
			commandBuffer, 0, 1, &vertexBuffer, &k_nVertexBufferOffset);

		const uint32 k_nPushConstantStageFlags =
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		if (bHasValidFrame)
		{
			// Draw exactly what the managed render flow submitted.
			const ArrayList<RenderCore::TriangleDrawCommand>& drawCommands =
				renderCore.GetTriangleDrawCommands();
			for (size_t nCommandIndex = 0; nCommandIndex < drawCommands.GetSize(); ++nCommandIndex)
			{
				PushConstants pushConstants;
				BuildPushConstants(drawCommands[nCommandIndex], pushConstants);
				vkCmdPushConstants(
					commandBuffer,
					m_TrianglePipeline.GetLayout(),
					k_nPushConstantStageFlags,
					0,
					sizeof(PushConstants),
					&pushConstants);
				vkCmdDraw(commandBuffer, static_cast<uint32>(k_nTriangleVertexCount), 1, 0, 0);
			}
		}
		else
		{
			// Default frame: multicolor triangle at the origin.
			RenderCore::TriangleDrawCommand defaultCommand;
			PushConstants pushConstants;
			BuildPushConstants(defaultCommand, pushConstants);
			vkCmdPushConstants(
				commandBuffer,
				m_TrianglePipeline.GetLayout(),
				k_nPushConstantStageFlags,
				0,
				sizeof(PushConstants),
				&pushConstants);
			vkCmdDraw(commandBuffer, static_cast<uint32>(k_nTriangleVertexCount), 1, 0, 0);
		}

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

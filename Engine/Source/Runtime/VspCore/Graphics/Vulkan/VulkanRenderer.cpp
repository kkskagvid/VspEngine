#include "RuntimePCH.h"

#include <cstdio>
#include <cstring>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Core/Logging/Log.h"
#include "Graphics/Vulkan/VulkanRenderer.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "VulkanRenderer";

	// Both programmable stages may read the push-constant block.
	static constexpr uint32 k_nPushConstantStageFlags =
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	// -------------------------------------------------------------------------
	// Resource tables
	// -------------------------------------------------------------------------

	template <typename EntryType>
	EntryType* VulkanRenderer::FindResource(ArrayList<EntryType>& Table, uint32 uHandle)
	{
		if (uHandle == k_nInvalidRhiHandle || uHandle > Table.GetSize())
		{
			return nullptr;
		}

		EntryType& entry = Table[uHandle - 1u];
		return entry.bIsActive ? &entry : nullptr;
	}

	template <typename EntryType>
	const EntryType* VulkanRenderer::FindResource(const ArrayList<EntryType>& Table, uint32 uHandle) const
	{
		return const_cast<VulkanRenderer*>(this)->FindResource(
			const_cast<ArrayList<EntryType>&>(Table), uHandle);
	}

	bool VulkanRenderer::GetPipelineShaderStage(
		RhiShaderHandle uShader,
		RhiShaderStage eStage,
		PipelineShaderStage& outStage) const
	{
		const ShaderEntry* pEntry = FindResource(m_Shaders, uShader);
		if (pEntry == nullptr)
		{
			LOG_ERROR(kLogTag, "The shader handle {} is not live.", uShader);
			return false;
		}
		if (pEntry->eStage != eStage)
		{
			LOG_ERROR(kLogTag, "The shader handle {} was created for the other pipeline stage.", uShader);
			return false;
		}

		outStage.VkShader = pEntry->VkShader;
		outStage.pEntryPointName = pEntry->EntryPointName;
		return true;
	}

	uint32 VulkanRenderer::AcquireBindlessTextureSlot() const
	{
		// The lowest slot no live texture occupies; the bindless array has
		// VulkanDescriptors::k_nMaxBindlessTextureCount entries.
		for (uint32 uSlot = 0; uSlot < VulkanDescriptors::k_nMaxBindlessTextureCount; ++uSlot)
		{
			bool bIsOccupied = false;
			for (size_t nTextureIndex = 0; nTextureIndex < m_Textures.GetSize(); ++nTextureIndex)
			{
				if (m_Textures[nTextureIndex].bIsActive && m_Textures[nTextureIndex].uBindlessSlot == uSlot)
				{
					bIsOccupied = true;
					break;
				}
			}

			if (!bIsOccupied)
			{
				return uSlot;
			}
		}
		return UINT32_MAX;
	}

	// -------------------------------------------------------------------------
	// Lifecycle
	// -------------------------------------------------------------------------

	VulkanRenderer::~VulkanRenderer()
	{
		Shutdown();
	}

	bool VulkanRenderer::Initialize(void* pNativeWindowHandle)
	{
		// 1. Context facade (instance + surface + device), then the swapchain
		//    and the per-frame resources.
		if (!m_Context.Initialize("Vsp Engine", pNativeWindowHandle))
		{
			// Device below Vulkan 1.3 lands here: the context has already
			// logged the "Unsupported device" details through the Log module.
			return false;
		}

		if (!m_SwapChain.Initialize(m_Context, 1280, 720)) return false;
		if (!CreateFrameResources()) return false;

		// 2. Bindless descriptors: layout + pool + one set per frame. The
		//    texture slots stay empty until the managed render pipeline creates
		//    its textures through CreateTexture.
		const VulkanBuffer* ppFrameUniformBuffers[k_nMaxFramesInFlight] =
		{
			&m_Frames[0].CameraUniformBuffer,
			&m_Frames[1].CameraUniformBuffer,
		};
		if (!m_BindlessDescriptors.Create(m_Context, k_nMaxFramesInFlight, ppFrameUniformBuffers))
		{
			return false;
		}

		// Nothing else is created here: buffers, shaders, textures and
		// pipelines all come from the managed render pipeline through the
		// wrapped graphics API.
		m_bIsInitialized = true;
		LOG_INFO(kLogTag, "Renderer ready (feature path: Vulkan 1.3 bindless); waiting for the managed render pipeline.");
		return true;
	}

	void VulkanRenderer::Shutdown()
	{
		if (m_Context.IsInitialized())
		{
			m_Context.WaitIdle();
		}

		// Pipelines reference the swapchain render pass, the resources
		// reference the descriptor sets and the frame resources own the
		// per-frame uniform buffers, so teardown walks the graph backwards.
		DestroyPipelines();
		DestroyBuffers();
		DestroyTextures();
		DestroyShaders();
		DestroyFrameResources();
		m_BindlessDescriptors.Destroy(m_Context);

		m_SwapChain.Destroy();
		m_Context.Destroy();
		m_bIsInitialized = false;
	}

	void VulkanRenderer::OnWindowResize(uint32 uWidth, uint32 uHeight)
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
			// Note: the render pass survives a recreate (same format, same
			// sample count), so the pipelines the managed side created stay
			// valid across resizes.
		}
	}

	void VulkanRenderer::DestroyBuffers()
	{
		if (!m_Context.IsInitialized())
		{
			m_Buffers.Clear();
			return;
		}

		for (size_t nEntryIndex = 0; nEntryIndex < m_Buffers.GetSize(); ++nEntryIndex)
		{
			if (m_Buffers[nEntryIndex].bIsActive)
			{
				m_Buffers[nEntryIndex].Buffer.Destroy(m_Context);
				m_Buffers[nEntryIndex].bIsActive = false;
			}
		}
		m_Buffers.Clear();
	}

	void VulkanRenderer::DestroyShaders()
	{
		if (!m_Context.IsInitialized())
		{
			m_Shaders.Clear();
			return;
		}

		const VkDevice device = m_Context.GetDevice();
		for (size_t nEntryIndex = 0; nEntryIndex < m_Shaders.GetSize(); ++nEntryIndex)
		{
			ShaderEntry& entry = m_Shaders[nEntryIndex];
			if (entry.bIsActive && entry.VkShader != VK_NULL_HANDLE)
			{
				vkDestroyShaderModule(device, entry.VkShader, nullptr);
			}
			entry.VkShader = VK_NULL_HANDLE;
			entry.bIsActive = false;
		}
		m_Shaders.Clear();
	}

	void VulkanRenderer::DestroyTextures()
	{
		if (!m_Context.IsInitialized())
		{
			m_Textures.Clear();
			return;
		}

		for (size_t nEntryIndex = 0; nEntryIndex < m_Textures.GetSize(); ++nEntryIndex)
		{
			TextureEntry& entry = m_Textures[nEntryIndex];
			if (entry.bIsActive)
			{
				entry.Image.Destroy(m_Context);
				entry.bIsActive = false;
			}
		}
		m_Textures.Clear();
	}

	void VulkanRenderer::DestroyPipelines()
	{
		if (!m_Context.IsInitialized())
		{
			m_Pipelines.Clear();
			return;
		}

		for (size_t nEntryIndex = 0; nEntryIndex < m_Pipelines.GetSize(); ++nEntryIndex)
		{
			PipelineEntry& entry = m_Pipelines[nEntryIndex];
			if (entry.bIsActive)
			{
				entry.Pipeline.Destroy(m_Context);
				entry.bIsActive = false;
			}
		}
		m_Pipelines.Clear();
	}

	// -------------------------------------------------------------------------
	// Wrapped graphics API: buffers
	// -------------------------------------------------------------------------

	RhiBufferHandle VulkanRenderer::CreateBuffer(const RhiBufferDescriptor& descriptor)
	{
		if (!m_bIsInitialized)
		{
			LOG_ERROR(kLogTag, "CreateBuffer: the renderer is not initialized.");
			return k_nInvalidRhiHandle;
		}

		VkBufferUsageFlags eUsage = (descriptor.eUsage == RhiBufferUsage::Index)
			? VK_BUFFER_USAGE_INDEX_BUFFER_BIT
			: VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

		VkMemoryPropertyFlags eProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		if (descriptor.bIsDynamic)
		{
			// A dynamic buffer is written straight from the CPU every frame.
			eProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		}
		else
		{
			// A static buffer is filled through a staging copy.
			eUsage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		}

		BufferEntry entry;
		entry.bIsActive = true;
		entry.bIsDynamic = descriptor.bIsDynamic;
		entry.nByteSize = descriptor.uByteSize;
		if (!entry.Buffer.Allocate(m_Context, descriptor.uByteSize, eUsage, eProperties))
		{
			return k_nInvalidRhiHandle;
		}

		// Recycle a released slot before growing the table.
		for (size_t nEntryIndex = 0; nEntryIndex < m_Buffers.GetSize(); ++nEntryIndex)
		{
			if (!m_Buffers[nEntryIndex].bIsActive)
			{
				m_Buffers[nEntryIndex] = std::move(entry);
				return static_cast<RhiBufferHandle>(nEntryIndex + 1u);
			}
		}

		m_Buffers.Add(std::move(entry));
		return static_cast<RhiBufferHandle>(m_Buffers.GetSize());
	}

	void VulkanRenderer::DestroyBuffer(RhiBufferHandle uBuffer)
	{
		BufferEntry* pEntry = FindResource(m_Buffers, uBuffer);
		if (pEntry == nullptr)
		{
			return;
		}

		m_Context.WaitIdle();
		pEntry->Buffer.Destroy(m_Context);
		pEntry->bIsActive = false;
		pEntry->bIsDynamic = false;
		pEntry->nByteSize = 0;
	}

	bool VulkanRenderer::UpdateBuffer(
		RhiBufferHandle uBuffer,
		uint32 uByteOffset,
		const void* pData,
		uint32 uByteCount)
	{
		BufferEntry* pEntry = FindResource(m_Buffers, uBuffer);
		if (pEntry == nullptr || pData == nullptr || uByteCount == 0)
		{
			LOG_ERROR(kLogTag, "UpdateBuffer: the buffer handle is not live or the range is empty.");
			return false;
		}
		if (static_cast<VkDeviceSize>(uByteOffset) + uByteCount > pEntry->nByteSize)
		{
			LOG_ERROR(kLogTag, "UpdateBuffer: the range [{}, {}) exceeds the {} byte buffer.",
				uByteOffset, uByteOffset + uByteCount, static_cast<uint32>(pEntry->nByteSize));
			return false;
		}

		if (pEntry->bIsDynamic)
		{
			return pEntry->Buffer.WriteData(m_Context, uByteOffset, pData, uByteCount);
		}

		// Static buffer: upload through a host-visible staging buffer and a
		// one-shot copy (the helper waits for the queue, so the buffer is
		// ready before this call returns).
		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
		m_Context.AllocateBuffer(
			uByteCount,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			stagingBuffer,
			stagingBufferMemory);
		if (stagingBuffer == VK_NULL_HANDLE)
		{
			return false;
		}

		// A failed map leaves the pointer null, so it is checked before the copy:
		// otherwise the upload of a static buffer would crash instead of failing.
		void* pMappedData = nullptr;
		const VkResult eMapResult =
			vkMapMemory(m_Context.GetDevice(), stagingBufferMemory, 0, uByteCount, 0, &pMappedData);
		if (eMapResult != VK_SUCCESS || pMappedData == nullptr)
		{
			LOG_ERROR(kLogTag, "UpdateBuffer: vkMapMemory failed with {} for {} byte(s).",
				static_cast<int32>(eMapResult), uByteCount);
			vkDestroyBuffer(m_Context.GetDevice(), stagingBuffer, nullptr);
			vkFreeMemory(m_Context.GetDevice(), stagingBufferMemory, nullptr);
			return false;
		}

		memcpy(pMappedData, pData, uByteCount);
		vkUnmapMemory(m_Context.GetDevice(), stagingBufferMemory);

		VkCommandBuffer commandBuffer = m_Context.BeginOneTimeCommandBuffer();
		bool bSuccess = (commandBuffer != VK_NULL_HANDLE);
		if (bSuccess)
		{
			VkBufferCopy copyRegion = {};
			copyRegion.srcOffset = 0;
			copyRegion.dstOffset = uByteOffset;
			copyRegion.size = uByteCount;
			vkCmdCopyBuffer(commandBuffer, stagingBuffer, pEntry->Buffer.GetBuffer(), 1, &copyRegion);
			bSuccess = m_Context.EndOneTimeCommandBuffer(commandBuffer);
		}

		vkDestroyBuffer(m_Context.GetDevice(), stagingBuffer, nullptr);
		vkFreeMemory(m_Context.GetDevice(), stagingBufferMemory, nullptr);
		return bSuccess;
	}

	// -------------------------------------------------------------------------
	// Wrapped graphics API: shaders
	// -------------------------------------------------------------------------

	RhiShaderHandle VulkanRenderer::CreateShader(
		RhiShaderStage eStage,
		const char* pEntryPointName,
		const void* pSpirvCode,
		uint32 uByteCount)
	{
		if (!m_bIsInitialized)
		{
			LOG_ERROR(kLogTag, "CreateShader: the renderer is not initialized.");
			return k_nInvalidRhiHandle;
		}
		if (pEntryPointName == nullptr || *pEntryPointName == '\0')
		{
			LOG_ERROR(kLogTag, "CreateShader: the entry point name is empty.");
			return k_nInvalidRhiHandle;
		}

		const VkShaderModule shaderModule = m_Context.CreateShaderModule(
			static_cast<const uint32*>(pSpirvCode), uByteCount);
		if (shaderModule == VK_NULL_HANDLE)
		{
			return k_nInvalidRhiHandle;
		}

		ShaderEntry entry;
		entry.bIsActive = true;
		entry.eStage = eStage;
		entry.VkShader = shaderModule;
		strncpy_s(entry.EntryPointName, sizeof(entry.EntryPointName), pEntryPointName, _TRUNCATE);

		for (size_t nEntryIndex = 0; nEntryIndex < m_Shaders.GetSize(); ++nEntryIndex)
		{
			if (!m_Shaders[nEntryIndex].bIsActive)
			{
				m_Shaders[nEntryIndex] = entry;
				return static_cast<RhiShaderHandle>(nEntryIndex + 1u);
			}
		}

		m_Shaders.Add(entry);
		return static_cast<RhiShaderHandle>(m_Shaders.GetSize());
	}

	void VulkanRenderer::DestroyShader(RhiShaderHandle uShader)
	{
		ShaderEntry* pEntry = FindResource(m_Shaders, uShader);
		if (pEntry == nullptr)
		{
			return;
		}

		m_Context.WaitIdle();
		if (pEntry->VkShader != VK_NULL_HANDLE)
		{
			vkDestroyShaderModule(m_Context.GetDevice(), pEntry->VkShader, nullptr);
			pEntry->VkShader = VK_NULL_HANDLE;
		}
		pEntry->bIsActive = false;
	}

	// -------------------------------------------------------------------------
	// Wrapped graphics API: textures
	// -------------------------------------------------------------------------

	RhiTextureHandle VulkanRenderer::CreateTexture(
		const RhiTextureDescriptor& descriptor,
		const void* pPixelDataRgba8)
	{
		if (!m_bIsInitialized)
		{
			LOG_ERROR(kLogTag, "CreateTexture: the renderer is not initialized.");
			return k_nInvalidRhiHandle;
		}

		const uint32 uBindlessSlot = AcquireBindlessTextureSlot();
		if (uBindlessSlot == UINT32_MAX)
		{
			LOG_ERROR(kLogTag, "CreateTexture: every bindless texture slot is taken.");
			return k_nInvalidRhiHandle;
		}

		TextureEntry entry;
		entry.bIsActive = true;
		entry.uBindlessSlot = uBindlessSlot;
		if (!entry.Image.Create(m_Context, descriptor.uWidth, descriptor.uHeight, pPixelDataRgba8))
		{
			return k_nInvalidRhiHandle;
		}

		// The descriptor sets are read by in-flight frames, so the update has
		// to wait for the device to go idle.
		m_Context.WaitIdle();
		if (!m_BindlessDescriptors.WriteTextureSlot(m_Context, uBindlessSlot, entry.Image.GetView()))
		{
			entry.Image.Destroy(m_Context);
			return k_nInvalidRhiHandle;
		}

		for (size_t nEntryIndex = 0; nEntryIndex < m_Textures.GetSize(); ++nEntryIndex)
		{
			if (!m_Textures[nEntryIndex].bIsActive)
			{
				m_Textures[nEntryIndex] = std::move(entry);
				return static_cast<RhiTextureHandle>(nEntryIndex + 1u);
			}
		}

		m_Textures.Add(std::move(entry));
		return static_cast<RhiTextureHandle>(m_Textures.GetSize());
	}

	void VulkanRenderer::DestroyTexture(RhiTextureHandle uTexture)
	{
		TextureEntry* pEntry = FindResource(m_Textures, uTexture);
		if (pEntry == nullptr)
		{
			return;
		}

		m_Context.WaitIdle();
		pEntry->Image.Destroy(m_Context);
		pEntry->bIsActive = false;
	}

	int32 VulkanRenderer::GetTextureBindlessSlot(RhiTextureHandle uTexture) const
	{
		const TextureEntry* pEntry = FindResource(m_Textures, uTexture);
		return pEntry != nullptr ? static_cast<int32>(pEntry->uBindlessSlot) : -1;
	}

	// -------------------------------------------------------------------------
	// Wrapped graphics API: pipelines
	// -------------------------------------------------------------------------

	RhiPipelineHandle VulkanRenderer::CreateGraphicsPipeline(const RhiGraphicsPipelineState& state)
	{
		if (!m_bIsInitialized)
		{
			LOG_ERROR(kLogTag, "CreateGraphicsPipeline: the renderer is not initialized.");
			return k_nInvalidRhiHandle;
		}

		PipelineShaderStage vertexStage;
		PipelineShaderStage fragmentStage;
		if (!GetPipelineShaderStage(state.uVertexShader, RhiShaderStage::Vertex, vertexStage) ||
			!GetPipelineShaderStage(state.uFragmentShader, RhiShaderStage::Fragment, fragmentStage))
		{
			return k_nInvalidRhiHandle;
		}

		PipelineEntry entry;
		entry.bIsActive = true;
		if (!entry.Pipeline.Create(
			m_Context,
			m_SwapChain.GetRenderPass(),
			m_BindlessDescriptors.GetLayout(),
			vertexStage,
			fragmentStage,
			state))
		{
			return k_nInvalidRhiHandle;
		}

		for (size_t nEntryIndex = 0; nEntryIndex < m_Pipelines.GetSize(); ++nEntryIndex)
		{
			if (!m_Pipelines[nEntryIndex].bIsActive)
			{
				m_Pipelines[nEntryIndex] = std::move(entry);
				return static_cast<RhiPipelineHandle>(nEntryIndex + 1u);
			}
		}

		m_Pipelines.Add(std::move(entry));
		return static_cast<RhiPipelineHandle>(m_Pipelines.GetSize());
	}

	void VulkanRenderer::DestroyGraphicsPipeline(RhiPipelineHandle uPipeline)
	{
		PipelineEntry* pEntry = FindResource(m_Pipelines, uPipeline);
		if (pEntry == nullptr)
		{
			return;
		}

		m_Context.WaitIdle();
		pEntry->Pipeline.Destroy(m_Context);
		pEntry->bIsActive = false;
	}

	void VulkanRenderer::GetBackbufferExtent(uint32& outWidth, uint32& outHeight) const
	{
		const VkExtent2D extent = m_SwapChain.GetExtent();
		outWidth = extent.width;
		outHeight = extent.height;
	}

	// -------------------------------------------------------------------------
	// FrameResources (command buffers, per-frame uniforms, sync primitives)
	// -------------------------------------------------------------------------

	bool VulkanRenderer::CreateFrameResources()
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

		if (m_SwapChain.GetImageCount() > k_nMaxSwapChainImageCount)
		{
			LOG_ERROR(kLogTag, "The swapchain has {} images, more than the {} the renderer keeps semaphores for.",
				m_SwapChain.GetImageCount(), k_nMaxSwapChainImageCount);
			return false;
		}

		VkSemaphoreCreateInfo semaphoreCreateInfo = {};
		semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceCreateInfo = {};
		fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;   // First frame passes the wait.

		// The presentation engine may still wait on the semaphore a submit
		// signaled until that image is presented again, so each swapchain image
		// owns its own render-finished semaphore.
		for (uint32 uImageIndex = 0; uImageIndex < k_nMaxSwapChainImageCount; ++uImageIndex)
		{
			if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &m_RenderFinishedSemaphores[uImageIndex]) != VK_SUCCESS)
			{
				LOG_ERROR(kLogTag, "Failed to create the render-finished semaphores.");
				return false;
			}
		}

		for (uint32 uFrameIndex = 0; uFrameIndex < k_nMaxFramesInFlight; ++uFrameIndex)
		{
			FrameResources& frame = m_Frames[uFrameIndex];
			frame.commandBuffer = commandBuffers[uFrameIndex];

			if (vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &frame.imageAvailableSemaphore) != VK_SUCCESS ||
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

	void VulkanRenderer::DestroyFrameResources()
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
			frame.CameraUniformBuffer.Destroy(m_Context);
		}

		for (uint32 uImageIndex = 0; uImageIndex < k_nMaxSwapChainImageCount; ++uImageIndex)
		{
			if (m_RenderFinishedSemaphores[uImageIndex] != VK_NULL_HANDLE)
			{
				vkDestroySemaphore(device, m_RenderFinishedSemaphores[uImageIndex], nullptr);
				m_RenderFinishedSemaphores[uImageIndex] = VK_NULL_HANDLE;
			}
		}

		VkCommandBuffer commandBuffers[k_nMaxFramesInFlight] = {};
		for (uint32 uFrameIndex = 0; uFrameIndex < k_nMaxFramesInFlight; ++uFrameIndex)
		{
			commandBuffers[uFrameIndex] = m_Frames[uFrameIndex].commandBuffer;
		}
		vkFreeCommandBuffers(device, m_Context.GetCommandPool(), k_nMaxFramesInFlight, commandBuffers);
	}

	// -------------------------------------------------------------------------
	// Frame rendering
	// -------------------------------------------------------------------------

	void VulkanRenderer::UpdateCameraUniform(uint32 uFrameIndex)
	{
		const VkExtent2D extent = m_SwapChain.GetExtent();
		if (extent.height == 0)
		{
			return;
		}

		CameraUniformData uniformData = {};

		const RhiFrameCamera& frameCamera = RenderCore::Get().GetFrameCamera();
		if (frameCamera.bIsSet)
		{
			// The camera the render pipeline chose for this frame.
			memcpy(uniformData.m4ViewProjection, frameCamera.m4ViewProjection, sizeof(uniformData.m4ViewProjection));
			memcpy(uniformData.m4View, frameCamera.m4View, sizeof(uniformData.m4View));
			memcpy(uniformData.m4Projection, frameCamera.m4Projection, sizeof(uniformData.m4Projection));
			memcpy(uniformData.m4CameraToWorld, frameCamera.m4CameraToWorld, sizeof(uniformData.m4CameraToWorld));

			uniformData.fCameraPosition[0] = frameCamera.fPosition[0];
			uniformData.fCameraPosition[1] = frameCamera.fPosition[1];
			uniformData.fCameraPosition[2] = frameCamera.fPosition[2];
			uniformData.fCameraPosition[3] = frameCamera.fAperture;

			uniformData.fLens[0] = frameCamera.fFocusDistance;
			uniformData.fLens[1] = frameCamera.fNearClipPlane;
			uniformData.fLens[2] = frameCamera.fFarClipPlane;
		}
		else
		{
			// No pipeline handed a camera over: fall back to the projection the
			// engine used before cameras existed - an orthographic box that
			// preserves the window aspect ratio, with world units in [-1, 1] on
			// the shorter axis. Vulkan's viewport transform maps NDC +Y to the
			// BOTTOM of the framebuffer (it uses (y+1)/2, unlike OpenGL), so the
			// ortho's top/bottom are swapped: world +Y ends up at the top.
			const float fAspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
			const glm::mat4 projectionMatrix = glm::ortho(-fAspectRatio, fAspectRatio, 1.0f, -1.0f, -1.0f, 1.0f);
			memcpy(uniformData.m4ViewProjection, &projectionMatrix[0][0], sizeof(uniformData.m4ViewProjection));
			memcpy(uniformData.m4Projection, &projectionMatrix[0][0], sizeof(uniformData.m4Projection));

			uniformData.m4View[0] = 1.0f;
			uniformData.m4View[5] = 1.0f;
			uniformData.m4View[10] = 1.0f;
			uniformData.m4View[15] = 1.0f;
			uniformData.m4CameraToWorld[0] = 1.0f;
			uniformData.m4CameraToWorld[5] = 1.0f;
			uniformData.m4CameraToWorld[10] = 1.0f;
			uniformData.m4CameraToWorld[15] = 1.0f;

			uniformData.fLens[1] = -1.0f;
			uniformData.fLens[2] = 1.0f;
		}

		m_Frames[uFrameIndex].CameraUniformBuffer.WriteData(m_Context, 0, &uniformData, sizeof(CameraUniformData));
	}

	void VulkanRenderer::RecordFullExtentViewportAndScissor(VkCommandBuffer commandBuffer)
	{
		const VkExtent2D extent = m_SwapChain.GetExtent();

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
	}

	void VulkanRenderer::RecordRenderPassBegin(VkCommandBuffer commandBuffer, uint32 uImageIndex)
	{
		// The clear color is the frame's clear color: BeginFrame /
		// SetClearColor / EndFrame wrap the whole command list, so it applies
		// to every render pass the pipeline opens.
		const RenderCore::FrameClearColor& clearColor = RenderCore::Get().GetClearColor();

		const VkExtent2D extent = m_SwapChain.GetExtent();

		VkRenderPassBeginInfo renderPassBeginInfo = {};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = m_SwapChain.GetRenderPass();
		renderPassBeginInfo.framebuffer = m_SwapChain.GetFramebuffer(uImageIndex);
		renderPassBeginInfo.renderArea.offset = { 0, 0 };
		renderPassBeginInfo.renderArea.extent = extent;

		// One clear value per attachment: the color target and the depth buffer
		// (cleared to the far plane, which is 1 in Vulkan's depth convention).
		VkClearValue clearValues[2] = {};
		clearValues[0].color = { { clearColor.fColorR, clearColor.fColorG, clearColor.fColorB, clearColor.fColorA } };
		clearValues[1].depthStencil = { 1.0f, 0 };
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		// A render pass starts with the viewport and scissor covering the whole
		// attachment; the pipeline may override them per frame.
		RecordFullExtentViewportAndScissor(commandBuffer);
	}

	void VulkanRenderer::RecordDefaultRenderPass(VkCommandBuffer commandBuffer, uint32 uImageIndex)
	{
		// Used when the managed render pipeline submitted no usable frame (or
		// a frame without a render pass): the window still clears to the
		// frame's clear color instead of showing stale pixels.
		RecordRenderPassBegin(commandBuffer, uImageIndex);
		vkCmdEndRenderPass(commandBuffer);
	}

	bool VulkanRenderer::PlaybackCommands(VkCommandBuffer commandBuffer, uint32 uImageIndex)
	{
		const ArrayList<RhiCommand>& commands = RenderCore::Get().GetCommands();

		VkPipelineLayout boundPipelineLayout = VK_NULL_HANDLE;
		bool bRenderPassActive = false;

		// Whether the command list opened a render pass at all: when it did,
		// the caller must not add its fallback clear pass on top of it.
		bool bRenderPassSeen = false;

		for (size_t nCommandIndex = 0; nCommandIndex < commands.GetSize(); ++nCommandIndex)
		{
			const RhiCommand& command = commands[nCommandIndex];
			switch (command.eType)
			{
			case RhiCommandType::BeginRenderPass:
				if (!bRenderPassActive)
				{
					RecordRenderPassBegin(commandBuffer, uImageIndex);
					bRenderPassActive = true;
					bRenderPassSeen = true;
				}
				break;

			case RhiCommandType::EndRenderPass:
				if (bRenderPassActive)
				{
					vkCmdEndRenderPass(commandBuffer);
					bRenderPassActive = false;
					boundPipelineLayout = VK_NULL_HANDLE;
				}
				break;

			case RhiCommandType::SetViewport:
			{
				VkViewport viewport = {};
				viewport.x = command.fValue[0];
				viewport.y = command.fValue[1];
				viewport.width = command.fValue[2];
				viewport.height = command.fValue[3];
				viewport.minDepth = 0.0f;
				viewport.maxDepth = 1.0f;
				vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
				break;
			}

			case RhiCommandType::SetScissor:
			{
				VkRect2D scissor = {};
				scissor.offset = { static_cast<int32>(command.uValueA), static_cast<int32>(command.uValueB) };
				scissor.extent = { command.uValueC, command.uValueD };
				vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
				break;
			}

			case RhiCommandType::BindPipeline:
			{
				const PipelineEntry* pEntry = FindResource(m_Pipelines, command.uResourceHandle);
				if (pEntry == nullptr || !pEntry->Pipeline.IsValid())
				{
					LOG_WARNING(kLogTag, "Frame playback: pipeline handle {} is not live; the command is skipped.",
						command.uResourceHandle);
					break;
				}

				boundPipelineLayout = pEntry->Pipeline.GetLayout();
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pEntry->Pipeline.GetPipeline());

				// The pipeline layout always carries the bindless set at index
				// 0, so binding it with the pipeline keeps the two in sync.
				const VkDescriptorSet descriptorSet = m_BindlessDescriptors.GetSet(m_nCurrentFrameIndex);
				vkCmdBindDescriptorSets(
					commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, boundPipelineLayout,
					0, 1, &descriptorSet, 0, nullptr);
				break;
			}

			case RhiCommandType::BindVertexBuffer:
			{
				const BufferEntry* pEntry = FindResource(m_Buffers, command.uResourceHandle);
				if (pEntry == nullptr)
				{
					LOG_WARNING(kLogTag, "Frame playback: vertex buffer handle {} is not live; the command is skipped.",
						command.uResourceHandle);
					break;
				}

				const VkDeviceSize k_nVertexBufferOffset = 0;
				const VkBuffer vertexBuffer = pEntry->Buffer.GetBuffer();
				vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &k_nVertexBufferOffset);
				break;
			}

			case RhiCommandType::BindIndexBuffer:
			{
				const BufferEntry* pEntry = FindResource(m_Buffers, command.uResourceHandle);
				if (pEntry == nullptr)
				{
					LOG_WARNING(kLogTag, "Frame playback: index buffer handle {} is not live; the command is skipped.",
						command.uResourceHandle);
					break;
				}

				vkCmdBindIndexBuffer(commandBuffer, pEntry->Buffer.GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
				break;
			}

			case RhiCommandType::PushConstants:
				if (boundPipelineLayout != VK_NULL_HANDLE && command.uValueC > 0)
				{
					vkCmdPushConstants(
						commandBuffer,
						boundPipelineLayout,
						command.uValueA != 0 ? command.uValueA : k_nPushConstantStageFlags,
						command.uValueB,
						command.uValueC,
						command.PushConstantBytes);
				}
				break;

			case RhiCommandType::Draw:
				if (bRenderPassActive && command.uValueA > 0)
				{
					vkCmdDraw(commandBuffer, command.uValueA, 1, command.uValueB, 0);
				}
				break;

			case RhiCommandType::DrawIndexed:
				if (bRenderPassActive && command.uValueA > 0)
				{
					vkCmdDrawIndexed(commandBuffer, command.uValueA, 1, command.uValueB, 0, command.uValueC);
				}
				break;

			default:
				break;
			}
		}

		if (bRenderPassActive)
		{
			vkCmdEndRenderPass(commandBuffer);
		}
		return bRenderPassSeen;
	}

	void VulkanRenderer::RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32 uImageIndex)
	{
		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		const RenderCore& renderCore = RenderCore::Get();
		bool bRenderPassRecorded = false;

		if (renderCore.IsFrameValid())
		{
			bRenderPassRecorded = PlaybackCommands(commandBuffer, uImageIndex);
		}

		if (!bRenderPassRecorded)
		{
			// No usable frame (or one without a render pass): fall back to a
			// plain clear so the window always shows the frame's background.
			RecordDefaultRenderPass(commandBuffer, uImageIndex);
		}

		vkEndCommandBuffer(commandBuffer);
	}

	bool VulkanRenderer::RenderFrame()
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

		const uint32 uImageIndex = m_SwapChain.GetCurrentImageIndex();
		vkResetCommandBuffer(frame.commandBuffer, 0);
		RecordCommandBuffer(frame.commandBuffer, uImageIndex);

		bool bIsOutOfDate = false;
		if (!m_SwapChain.SubmitAndPresent(
			frame.commandBuffer,
			frame.imageAvailableSemaphore,
			m_RenderFinishedSemaphores[uImageIndex],
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

	bool VulkanRenderer::CaptureFramebuffer(const VspString& sFilePath)
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

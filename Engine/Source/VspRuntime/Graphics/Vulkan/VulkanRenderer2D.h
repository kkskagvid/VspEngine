#pragma once

#include <vulkan/vulkan.h>

#include "Graphics/IGraphics.h"
#include "Graphics/Vulkan/VulkanRHI.h"
#include "Graphics/Vulkan/VulkanSwapChain.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// VulkanRenderer2D
	// -------------------------------------------------------------------------
	// Minimal 2D renderer drawing one colored triangle, used to validate the
	// engine. Depending on the negotiated device capabilities it runs one of
	// two implementations:
	//   - Vulkan13Bindless  : Vulkan 1.3 with descriptor indexing; the texture
	//                         is sampled out of a large bindless array through
	//                         nonuniformEXT.
	//   - Vulkan12Fallback  : classic descriptor sets (combined image sampler),
	//                         compatible with plain Vulkan 1.2 devices.
	// The triangle position and color mode are set every frame by the game
	// engine based on what the managed scripts reported.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // Members of non-dll-interface helper classes.
	class RUNTIME_API VulkanRenderer2D : public IGraphics
	{
	public:
		static constexpr uint32_t k_nMaxFramesInFlight = 2;
		static constexpr uint32_t k_nMaxBindlessTextureCount = 4096;

		// One triangle vertex: position, RGBA color, UV.
		struct Vertex2D
		{
			float fPositionX;
			float fPositionY;
			float fColorR;
			float fColorG;
			float fColorB;
			float fColorA;
			float fUvU;
			float fUvV;
		};

		// Pushed every draw: color override + mode + bindless texture slot.
		struct PushConstants
		{
			float fOverrideColorR;
			float fOverrideColorG;
			float fOverrideColorB;
			float fOverrideColorA;
			int32_t nColorMode;
			uint32_t uTextureIndex;
		};

		~VulkanRenderer2D() override;

		bool Initialize(void* pNativeWindowHandle, VspString& outErrorText) override;
		void Shutdown() override;
		void OnWindowResize(uint32_t uWidth, uint32_t uHeight) override;
		bool RenderFrame(VspString& outErrorText) override;

		void SetTrianglePosition(float fPositionX, float fPositionY);
		void SetColorMode(int32_t nColorMode);

		// Reads the most recently presented swapchain image back to the CPU and
		// writes it as a 32-bit BMP (used by automated acceptance tests).
		bool CaptureFramebuffer(const VspString& sFilePath, VspString& outErrorText);

		VulkanFeaturePath GetFeaturePath() const { return m_Context.GetDeviceProperties().eFeaturePath; }
		const VulkanDeviceProperties& GetDeviceProperties() const { return m_Context.GetDeviceProperties(); }

	private:
		struct FrameResources
		{
			VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
			VkBuffer uniformBuffer = VK_NULL_HANDLE;
			VkDeviceMemory uniformBufferMemory = VK_NULL_HANDLE;
			VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
			VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
			VkFence inFlightFence = VK_NULL_HANDLE;
		};

		struct CameraUniformData
		{
			float m4ViewProjection[16];   // std140 mat4
		};

		// -------- Device / swapchain --------
		VulkanContext m_Context;
		VulkanSwapChain m_SwapChain;
		FrameResources m_Frames[k_nMaxFramesInFlight];
		uint32_t m_nCurrentFrameIndex = 0;

		// -------- Triangle geometry --------
		VkBuffer m_VkVertexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory m_VkVertexBufferMemory = VK_NULL_HANDLE;

		// -------- Demo texture (white) --------
		VkImage m_VkTextureImage = VK_NULL_HANDLE;
		VkDeviceMemory m_VkTextureMemory = VK_NULL_HANDLE;
		VkImageView m_VkTextureView = VK_NULL_HANDLE;
		VkSampler m_VkTextureSampler = VK_NULL_HANDLE;

		// -------- Bindless descriptors (Vulkan 1.3 path) --------
		VkDescriptorSetLayout m_VkBindlessDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_VkBindlessDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet m_VkBindlessDescriptorSets[k_nMaxFramesInFlight] = {};

		// -------- Fallback descriptors (Vulkan 1.2 path) --------
		VkDescriptorSetLayout m_VkFallbackDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_VkFallbackDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet m_VkFallbackDescriptorSets[k_nMaxFramesInFlight] = {};

		// -------- Pipelines --------
		VkPipelineLayout m_VkBindlessPipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_VkBindlessPipeline = VK_NULL_HANDLE;
		VkPipelineLayout m_VkFallbackPipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_VkFallbackPipeline = VK_NULL_HANDLE;

		// -------- Script-driven state --------
		float m_fTrianglePositionX = 0.0f;
		float m_fTrianglePositionY = 0.0f;
		int32_t m_nColorMode = 3;   // MultiColor
		bool m_bIsMinimized = false;
		bool m_bIsInitialized = false;

		// -------- Creation helpers --------
		bool CreateFrameResources(VspString& outErrorText);
		bool CreateTriangleGeometry(VspString& outErrorText);
		bool CreateDemoTexture(VspString& outErrorText);
		bool CreateBindlessDescriptors(VspString& outErrorText);
		bool CreateFallbackDescriptors(VspString& outErrorText);
		bool CreatePipelines(VspString& outErrorText);
		bool CreateGraphicsPipeline(
			VkShaderModule vertexShader,
			VkShaderModule fragmentShader,
			VkPipelineLayout pipelineLayout,
			VkPipeline& outPipeline,
			VspString& outErrorText) const;

		// -------- Frame helpers --------
		void DestroyFrameResources();
		void DestroyTriangleGeometry();
		void DestroyDemoTexture();
		void DestroyDescriptors();
		void DestroyPipelines();

		void UpdateCameraUniform(uint32_t uFrameIndex);
		void BuildPushConstants(PushConstants& outPushConstants) const;
		void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t uImageIndex);

		static bool TransitionImageLayout(
			const VulkanContext& context,
			VkImage image,
			VkFormat format,
			VkImageLayout oldLayout,
			VkImageLayout newLayout,
			VspString& outErrorText);
	};
#pragma warning(pop)
}

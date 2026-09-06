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
	// engine. It only runs the Vulkan 1.3 bindless implementation: the texture
	// is sampled out of a large bindless array through nonuniformEXT. Devices
	// below Vulkan 1.3 (or without bindless descriptor indexing) are rejected
	// during device negotiation - there is no Vulkan 1.2 fallback path.
	// The triangle position and color mode are set every frame by the game
	// engine based on what the managed scripts reported.
	// All errors are logged through the Log module; nothing throws.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // Members of non-dll-interface helper classes.
	class RUNTIME_API VulkanRenderer2D : public IGraphics
	{
	public:
		static constexpr uint32 k_nMaxFramesInFlight = 2;
		static constexpr uint32 k_nMaxBindlessTextureCount = 4096;

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
			int32 nColorMode;
			uint32 uTextureIndex;
		};

		~VulkanRenderer2D() override;

		bool Initialize(void* pNativeWindowHandle) override;
		void Shutdown() override;
		void OnWindowResize(uint32 uWidth, uint32 uHeight) override;
		bool RenderFrame() override;

		void SetTrianglePosition(float fPositionX, float fPositionY);
		void SetColorMode(int32 nColorMode);

		// Reads the most recently presented swapchain image back to the CPU and
		// writes it as a 32-bit BMP (used by automated acceptance tests).
		bool CaptureFramebuffer(const VspString& sFilePath);

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
		uint32 m_nCurrentFrameIndex = 0;

		// -------- Triangle geometry --------
		VkBuffer m_VkVertexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory m_VkVertexBufferMemory = VK_NULL_HANDLE;

		// -------- Demo texture (white) --------
		VkImage m_VkTextureImage = VK_NULL_HANDLE;
		VkDeviceMemory m_VkTextureMemory = VK_NULL_HANDLE;
		VkImageView m_VkTextureView = VK_NULL_HANDLE;
		VkSampler m_VkTextureSampler = VK_NULL_HANDLE;

		// -------- Bindless descriptors (the only supported path) --------
		VkDescriptorSetLayout m_VkDescriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorPool m_VkDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSet m_VkDescriptorSets[k_nMaxFramesInFlight] = {};

		// -------- Pipeline --------
		VkPipelineLayout m_VkPipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_VkPipeline = VK_NULL_HANDLE;

		// -------- Script-driven state --------
		float m_fTrianglePositionX = 0.0f;
		float m_fTrianglePositionY = 0.0f;
		int32 m_nColorMode = 3;   // MultiColor
		bool m_bIsMinimized = false;
		bool m_bIsInitialized = false;

		// -------- Creation helpers --------
		bool CreateFrameResources();
		bool CreateTriangleGeometry();
		bool CreateDemoTexture();
		bool CreateBindlessDescriptors();
		bool CreatePipelines();
		bool CreateGraphicsPipeline(
			VkShaderModule vertexShader,
			VkShaderModule fragmentShader,
			VkPipelineLayout pipelineLayout,
			VkPipeline& outPipeline) const;

		// -------- Frame helpers --------
		void DestroyFrameResources();
		void DestroyTriangleGeometry();
		void DestroyDemoTexture();
		void DestroyDescriptors();
		void DestroyPipelines();

		void UpdateCameraUniform(uint32 uFrameIndex);
		void BuildPushConstants(PushConstants& outPushConstants) const;
		void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32 uImageIndex);

		static bool TransitionImageLayout(
			const VulkanContext& context,
			VkImage image,
			VkFormat format,
			VkImageLayout oldLayout,
			VkImageLayout newLayout);
	};
#pragma warning(pop)
}

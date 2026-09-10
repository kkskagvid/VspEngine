#pragma once

#include <vulkan/vulkan.h>

#include "Graphics/IGraphics.h"
#include "Graphics/RenderCore.h"
#include "Graphics/Vulkan/VulkanBuffer.h"
#include "Graphics/Vulkan/VulkanDescriptors.h"
#include "Graphics/Vulkan/VulkanImage.h"
#include "Graphics/Vulkan/VulkanPipeline.h"
#include "Graphics/Vulkan/VulkanRHI.h"
#include "Graphics/Vulkan/VulkanSwapChain.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// VulkanRenderer2D
	// -------------------------------------------------------------------------
	// Minimal 2D renderer drawing triangles submitted by the managed render
	// flow. The Vulkan module is split into functional units - the renderer
	// is the orchestrator that owns and wires them together:
	//   - VulkanContext (instance + surface + device facade)
	//   - VulkanSwapChain (swapchain, image views, framebuffers, render pass)
	//   - VulkanBuffer   (vertex geometry, per-frame camera uniforms)
	//   - VulkanImage    (the demo texture: image + view + sampler)
	//   - VulkanDescriptors (bindless layout/pool/sets - the only supported path)
	//   - VulkanPipeline (pipeline layout + graphics pipeline)
	// RenderFrame() consumes the frame commands collected by RenderCore (the
	// managed VspEngine render flow submits them through NativeExports) and
	// records the swapchain command buffer from them.
	// It only runs the Vulkan 1.3 bindless implementation: devices below
	// Vulkan 1.3 (or without bindless descriptor indexing) are rejected
	// during device negotiation - there is no Vulkan 1.2 fallback path.
	// All errors are logged through the Log module; nothing throws.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // Members of non-dll-interface helper classes.
	class RUNTIME_API VulkanRenderer2D : public IGraphics
	{
	public:
		static constexpr uint32 k_nMaxFramesInFlight = 2;

		~VulkanRenderer2D() override;

		bool Initialize(void* pNativeWindowHandle) override;
		void Shutdown() override;
		void OnWindowResize(uint32 uWidth, uint32 uHeight) override;
		bool RenderFrame() override;

		// Reads the most recently presented swapchain image back to the CPU and
		// writes it as a 32-bit BMP (used by automated acceptance tests).
		bool CaptureFramebuffer(const VspString& sFilePath);

		VulkanFeaturePath GetFeaturePath() const { return m_Context.GetDeviceProperties().eFeaturePath; }
		const VulkanDeviceProperties& GetDeviceProperties() const { return m_Context.GetDeviceProperties(); }

	private:
		struct FrameResources
		{
			VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
			VulkanBuffer CameraUniformBuffer;
			VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
			VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
			VkFence inFlightFence = VK_NULL_HANDLE;
		};

		struct CameraUniformData
		{
			float m4ViewProjection[16];   // std140 mat4 (projection only: positions arrive as push constants)
		};

		// -------- Creation / destruction helpers --------
		bool CreateFrameResources();
		bool CreateTriangleGeometry();
		void DestroyFrameResources();
		void DestroyTriangleGeometry();

		// -------- Frame helpers --------
		void UpdateCameraUniform(uint32 uFrameIndex);
		void BuildPushConstants(
			const RenderCore::TriangleDrawCommand& drawCommand,
			PushConstants& outPushConstants) const;
		void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32 uImageIndex);

		// -------- Device / swapchain --------
		VulkanContext m_Context;
		VulkanSwapChain m_SwapChain;
		FrameResources m_Frames[k_nMaxFramesInFlight];
		uint32 m_nCurrentFrameIndex = 0;

		// -------- Triangle geometry --------
		VulkanBuffer m_TriangleVertexBuffer;

		// -------- Demo texture (white, used by the bindless path) --------
		VulkanImage m_DemoTexture;

		// -------- Bindless descriptors (the only supported path) --------
		VulkanDescriptors m_BindlessDescriptors;

		// -------- Pipeline --------
		VulkanPipeline m_TrianglePipeline;

		// -------- State --------
		bool m_bIsMinimized = false;
		bool m_bIsInitialized = false;
	};
#pragma warning(pop)
}

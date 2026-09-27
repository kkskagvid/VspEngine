#pragma once

#include <vulkan/vulkan.h>

#include "Core/Templates/ArrayList.h"
#include "Graphics/IGraphics.h"
#include "Graphics/RenderCore.h"
#include "Graphics/RhiTypes.h"
#include "Graphics/Vulkan/VulkanBuffer.h"
#include "Graphics/Vulkan/VulkanDescriptors.h"
#include "Graphics/Vulkan/VulkanImage.h"
#include "Graphics/Vulkan/VulkanPipeline.h"
#include "Graphics/Vulkan/VulkanRHI.h"
#include "Graphics/Vulkan/VulkanSwapChain.h"

namespace Vsp
{
	// -------------------------------------------------------------------------
	// VulkanRenderer
	// -------------------------------------------------------------------------
	// The Vulkan backend behind the wrapped graphics API. It is the
	// orchestrator that owns and wires the functional units together:
	//   - VulkanContext (instance + surface + device facade)
	//   - VulkanSwapChain (swapchain, image views, depth images, framebuffers,
	//     the render pass with its color AND depth attachments)
	//   - VulkanBuffer   (per-frame camera uniforms and every managed buffer)
	//   - VulkanImage    (textures created through CreateTexture)
	//   - VulkanDescriptors (bindless layout/pool/sets - the only supported path)
	//   - VulkanPipeline (graphics pipelines created through the pipeline builders)
	//
	// Two halves:
	//   - RenderFrame() plays back the command list recorded in RenderCore (the
	//     managed render pipeline submitted it through NativeExports) into the
	//     swapchain command buffer;
	//   - the Create*/Destroy* overrides implement the resource side of the
	//     wrapped graphics API the managed render pipeline calls.
	// It only runs the Vulkan 1.3 bindless implementation: devices below
	// Vulkan 1.3 (or without bindless descriptor indexing) are rejected
	// during device negotiation - there is no Vulkan 1.2 fallback path.
	//
	// The name says what it is: THE Vulkan renderer of the engine. It is not a
	// 2D renderer and never was one in practice - it serves whatever a render
	// pipeline records, into one render pass with a colour AND a depth
	// attachment, and the engine has no separate 2D path: 2D content is 3D
	// content on a plane, drawn through the same pass, the same depth buffer and
	// the same camera as everything else. A 3D renderer, a UI renderer and an
	// editor viewport all record into it through the same wrapped graphics API.
	// All errors are logged through the Log module; nothing throws and every
	// creation function returns an invalid (0) handle on failure.
	// -------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable : 4251)   // ArrayList members: header-only templates.
	class RUNTIME_API VulkanRenderer : public IGraphics
	{
	public:
		static constexpr uint32 k_nMaxFramesInFlight = 2;

		// The presentation engine keeps a reference to the semaphore a submit
		// signaled until the image is presented again, so there has to be one
		// render-finished semaphore PER SWAPCHAIN IMAGE - a per-frame-in-flight
		// semaphore would be re-signaled while still in use.
		static constexpr uint32 k_nMaxSwapChainImageCount = 8;

		~VulkanRenderer() override;

		// -------- Backend lifetime --------
		bool Initialize(void* pNativeWindowHandle) override;
		void Shutdown() override;
		void OnWindowResize(uint32 uWidth, uint32 uHeight) override;
		bool RenderFrame() override;

		// -------- Wrapped graphics API: buffers --------
		RhiBufferHandle CreateBuffer(const RhiBufferDescriptor& descriptor) override;
		void DestroyBuffer(RhiBufferHandle uBuffer) override;
		bool UpdateBuffer(RhiBufferHandle uBuffer, uint32 uByteOffset, const void* pData, uint32 uByteCount) override;

		// -------- Wrapped graphics API: shaders --------
		RhiShaderHandle CreateShader(
			RhiShaderStage eStage,
			const char* pEntryPointName,
			const void* pSpirvCode,
			uint32 uByteCount) override;
		void DestroyShader(RhiShaderHandle uShader) override;

		// -------- Wrapped graphics API: textures (bindless slots) --------
		RhiTextureHandle CreateTexture(const RhiTextureDescriptor& descriptor, const void* pPixelDataRgba8) override;
		void DestroyTexture(RhiTextureHandle uTexture) override;
		int32 GetTextureBindlessSlot(RhiTextureHandle uTexture) const override;

		// -------- Wrapped graphics API: pipelines --------
		RhiPipelineHandle CreateGraphicsPipeline(const RhiGraphicsPipelineState& state) override;
		void DestroyGraphicsPipeline(RhiPipelineHandle uPipeline) override;

		// -------- Wrapped graphics API: the back buffer --------
		void GetBackbufferExtent(uint32& outWidth, uint32& outHeight) const override;

		// Reads the most recently presented swapchain image back to the CPU and
		// writes it as a 32-bit BMP (used by automated acceptance tests).
		bool CaptureFramebuffer(const VspString& sFilePath);

		const VulkanDeviceProperties& GetDeviceProperties() const { return m_Context.GetDeviceProperties(); }

	private:
		// One buffer created through the wrapped graphics API.
		struct BufferEntry
		{
			bool bIsActive = false;
			bool bIsDynamic = false;
			VkDeviceSize nByteSize = 0;
			VulkanBuffer Buffer;
		};

		// One compiled shader module created through the wrapped graphics API,
		// together with the entry point the stage will run.
		struct ShaderEntry
		{
			bool bIsActive = false;
			RhiShaderStage eStage = RhiShaderStage::Vertex;
			VkShaderModule VkShader = VK_NULL_HANDLE;
			char EntryPointName[k_nMaxShaderEntryPointNameLength] = {};
		};

		// One texture; uBindlessSlot is where the shaders find it.
		struct TextureEntry
		{
			bool bIsActive = false;
			uint32 uBindlessSlot = 0;
			VulkanImage Image;
		};

		// One graphics pipeline created through the wrapped graphics API.
		struct PipelineEntry
		{
			bool bIsActive = false;
			VulkanPipeline Pipeline;
		};

		struct FrameResources
		{
			VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
			VulkanBuffer CameraUniformBuffer;
			VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
			VkFence inFlightFence = VK_NULL_HANDLE;
		};

		// The engine's camera block: binding 0 of the engine descriptor set. A
		// shader declares the members it needs (the first one is
		// "column_major float4x4 ViewProjectionMatrix"), and the render pipeline
		// decides WHICH camera fills it by handing one to the frame
		// (VspRhi_SetFrameCamera).
		struct CameraUniformData
		{
			float m4ViewProjection[16];
			float m4View[16];
			float m4Projection[16];
			float m4CameraToWorld[16];

			// xyz = world position, w = aperture as an f-number.
			float fCameraPosition[4];

			// x = focus distance, y = near clip plane, z = far clip plane.
			float fLens[4];
		};

		// -------- Creation / destruction helpers --------
		bool CreateFrameResources();
		void DestroyFrameResources();

		void DestroyBuffers();
		void DestroyShaders();
		void DestroyTextures();
		void DestroyPipelines();

		// -------- Resource tables --------
		// Slot lookup shared by every resource table: handles are 1-based
		// indices, 0 always means invalid.
		template <typename EntryType>
		EntryType* FindResource(ArrayList<EntryType>& Table, uint32 uHandle);

		template <typename EntryType>
		const EntryType* FindResource(const ArrayList<EntryType>& Table, uint32 uHandle) const;

		// ---- Pipeline builders are resolved through GraphicsSystem, so the
		// ---- backend only resolves its own handles here.
		bool GetPipelineShaderStage(RhiShaderHandle uShader, RhiShaderStage eStage, PipelineShaderStage& outStage) const;
		uint32 AcquireBindlessTextureSlot() const;

		// -------- Frame helpers --------
		void UpdateCameraUniform(uint32 uFrameIndex);
		void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32 uImageIndex);

		// Plays the commands recorded in RenderCore back into the command
		// buffer. Returns true when the command list opened a render pass
		// itself (the caller then does not need its default pass).
		bool PlaybackCommands(VkCommandBuffer commandBuffer, uint32 uImageIndex);

		void RecordDefaultRenderPass(VkCommandBuffer commandBuffer, uint32 uImageIndex);
		void RecordRenderPassBegin(VkCommandBuffer commandBuffer, uint32 uImageIndex);
		void RecordFullExtentViewportAndScissor(VkCommandBuffer commandBuffer);

		// -------- Device / swapchain --------
		VulkanContext m_Context;
		VulkanSwapChain m_SwapChain;
		FrameResources m_Frames[k_nMaxFramesInFlight];
		uint32 m_nCurrentFrameIndex = 0;

		// One render-finished semaphore per swapchain image (see
		// k_nMaxSwapChainImageCount); indexed by the acquired image.
		VkSemaphore m_RenderFinishedSemaphores[k_nMaxSwapChainImageCount] = {};

		// -------- Bindless descriptors (the only supported path) --------
		VulkanDescriptors m_BindlessDescriptors;

		// -------- Resources created through the wrapped graphics API --------
		ArrayList<BufferEntry> m_Buffers;
		ArrayList<ShaderEntry> m_Shaders;
		ArrayList<TextureEntry> m_Textures;
		ArrayList<PipelineEntry> m_Pipelines;

		// -------- State --------
		bool m_bIsMinimized = false;
		bool m_bIsInitialized = false;
	};
#pragma warning(pop)
}

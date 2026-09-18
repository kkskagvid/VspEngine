#pragma once

#include <vulkan/vulkan.h>

#include "Core/Core.h"
#include "Graphics/RhiTypes.h"

namespace Vsp
{
	class VulkanContext;

	// -------------------------------------------------------------------------
	// VulkanPipeline
	// -------------------------------------------------------------------------
	// Functional unit owning one 2D graphics pipeline: the pipeline layout
	// (bindless descriptor set + one push-constant range covering both
	// programmable stages) and the VkPipeline built from the shader modules and
	// the state the managed render pipeline requested.
	//
	// The render pass and the descriptor set layout belong to the backend (they
	// come from the swapchain and the bindless descriptor set) and are passed
	// in; everything else comes from RhiGraphicsPipelineState, so the managed
	// side stays in control of the programmable and fixed-function state.
	//
	// Every function logs its own errors through the Log module and never
	// throws.
	// -------------------------------------------------------------------------

	// One 2D vertex: position, RGBA color, UV. The managed render pipeline uses
	// the same layout (VspEngine.Rendering.Vertex2D) and describes it to the
	// backend through the pipeline builder.
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

	// Pushed for every draw. Bytes 0-7 carry the per-draw position offset
	// (vertex stage); bytes 8-31 the color override, mode and the bindless
	// texture slot (fragment stage). The GLSL blocks in both shader stages
	// declare the same explicit offsets, so one shared range covers them.
	struct PushConstants
	{
		float fPositionOffsetX;    // offset 0  (vertex stage)
		float fPositionOffsetY;    // offset 4  (vertex stage)
		float fOverrideColorR;     // offset 8  (fragment stage)
		float fOverrideColorG;     // offset 12 (fragment stage)
		float fOverrideColorB;     // offset 16 (fragment stage)
		float fOverrideColorA;     // offset 20 (fragment stage)
		int32 nColorMode;          // offset 24 (fragment stage)
		uint32 uTextureIndex;      // offset 28 (fragment stage)
	};

	class VulkanPipeline
	{
	public:
		// Push-constant block size of the engine's shaders (must match the
		// explicit GLSL block offsets: 0..32).
		static constexpr VkDeviceSize k_nShaderPushConstantByteSize = 32;

		VulkanPipeline() = default;
		~VulkanPipeline();

		// A pipeline owns its handles, so it moves but never copies; moving
		// leaves the source empty (Destroy() on it then does nothing).
		VulkanPipeline(const VulkanPipeline&) = delete;
		VulkanPipeline& operator=(const VulkanPipeline&) = delete;
		VulkanPipeline(VulkanPipeline&& Other) noexcept;
		VulkanPipeline& operator=(VulkanPipeline&& Other) noexcept;

		// Creates the pipeline layout and the graphics pipeline. The shader
		// modules stay owned by the backend's shader table.
		bool Create(
			const VulkanContext& context,
			VkRenderPass renderPass,
			VkDescriptorSetLayout descriptorSetLayout,
			VkShaderModule vertexShader,
			VkShaderModule fragmentShader,
			const RhiGraphicsPipelineState& state);

		void Destroy(const VulkanContext& context);

		bool IsValid() const { return m_VkPipeline != VK_NULL_HANDLE; }
		VkPipeline GetPipeline() const { return m_VkPipeline; }
		VkPipelineLayout GetLayout() const { return m_VkPipelineLayout; }

	private:
		static VkFormat GetVertexAttributeFormat(uint32 uComponentCount);
		static VkPrimitiveTopology GetPrimitiveTopology(RhiPrimitiveTopology eTopology);

		static bool CreateGraphicsPipeline(
			VkDevice device,
			VkRenderPass renderPass,
			VkShaderModule vertexShader,
			VkShaderModule fragmentShader,
			VkPipelineLayout pipelineLayout,
			const RhiGraphicsPipelineState& state,
			VkPipeline& outPipeline);

		VkPipelineLayout m_VkPipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_VkPipeline = VK_NULL_HANDLE;
	};
}

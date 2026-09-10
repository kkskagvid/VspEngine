#pragma once

#include <vulkan/vulkan.h>

#include "Core/Core.h"

namespace Vsp
{
	class VulkanContext;

	// -------------------------------------------------------------------------
	// VulkanPipeline
	// -------------------------------------------------------------------------
	// Functional unit owning the 2D graphics pipeline: the shader modules are
	// created from the embedded SPIR-V, the pipeline layout carries the
	// vertex + fragment push-constant ranges, and the VkPipeline is built
	// against the swapchain's render pass and the bindless descriptor set
	// layout. Vertex2D and PushConstants below are the pipeline's input
	// contract shared with the renderer.
	// Every function logs its own errors through the Log module and never
	// throws.
	// -------------------------------------------------------------------------

	// One 2D vertex: position, RGBA color, UV.
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
		// One shared push-constant range covering both stages (must match the
		// explicit GLSL block offsets: 0..32).
		static constexpr VkDeviceSize k_nPushConstantByteSize = 32;   // sizeof(PushConstants)

		~VulkanPipeline();

		// Creates the pipeline layout and the graphics pipeline (the SPIR-V is
		// taken from the embedded shader binary header). Returns false on
		// failure.
		bool Create(
			const VulkanContext& context,
			VkRenderPass renderPass,
			VkDescriptorSetLayout descriptorSetLayout);

		void Destroy(const VulkanContext& context);

		bool IsValid() const { return m_VkPipeline != VK_NULL_HANDLE; }
		VkPipeline GetPipeline() const { return m_VkPipeline; }
		VkPipelineLayout GetLayout() const { return m_VkPipelineLayout; }

	private:
		static bool CreateGraphicsPipeline(
			VkDevice device,
			VkRenderPass renderPass,
			VkShaderModule vertexShader,
			VkShaderModule fragmentShader,
			VkPipelineLayout pipelineLayout,
			VkPipeline& outPipeline);

		VkPipelineLayout m_VkPipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_VkPipeline = VK_NULL_HANDLE;
	};
}

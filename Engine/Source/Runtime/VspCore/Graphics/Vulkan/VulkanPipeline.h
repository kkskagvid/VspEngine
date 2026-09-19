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
	// Functional unit owning one graphics pipeline: the pipeline layout
	// (bindless descriptor set + one push-constant range covering both
	// programmable stages) and the VkPipeline built from the shader modules and
	// the state the managed render pipeline requested.
	//
	// The render pass and the descriptor set layout belong to the backend (they
	// come from the swapchain and the bindless descriptor set) and are passed
	// in; everything else comes from RhiGraphicsPipelineState, so the render
	// pipeline that requested it stays in control of the programmable and
	// fixed-function state - including the size of its push-constant block.
	//
	// Every function logs its own errors through the Log module and never
	// throws.
	// -------------------------------------------------------------------------

	// One programmable stage of a graphics pipeline: the shader module and the
	// named entry point inside it the stage runs.
	struct PipelineShaderStage
	{
		VkShaderModule VkShader = VK_NULL_HANDLE;
		const char* pEntryPointName = "main";
	};

	class VulkanPipeline
	{
	public:
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
			const PipelineShaderStage& vertexStage,
			const PipelineShaderStage& fragmentStage,
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
			const PipelineShaderStage& vertexStage,
			const PipelineShaderStage& fragmentStage,
			VkPipelineLayout pipelineLayout,
			const RhiGraphicsPipelineState& state,
			VkPipeline& outPipeline);

		VkPipelineLayout m_VkPipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_VkPipeline = VK_NULL_HANDLE;
	};
}

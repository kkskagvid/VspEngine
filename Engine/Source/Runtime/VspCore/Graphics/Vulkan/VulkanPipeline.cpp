#include "RuntimePCH.h"

#include <cstddef>

#include "Core/Logging/Log.h"
#include "Graphics/Vulkan/VulkanPipeline.h"
#include "Graphics/Vulkan/VulkanRHI.h"

namespace Vsp
{
	static constexpr const char* kLogTag = "VulkanPipeline";

	VulkanPipeline::~VulkanPipeline()
	{
		// See VulkanBuffer: Destroy() is the explicit teardown path; the
		// context always outlives the pipeline.
	}

	VulkanPipeline::VulkanPipeline(VulkanPipeline&& Other) noexcept
		: m_VkPipelineLayout(Other.m_VkPipelineLayout)
		, m_VkPipeline(Other.m_VkPipeline)
	{
		Other.m_VkPipelineLayout = VK_NULL_HANDLE;
		Other.m_VkPipeline = VK_NULL_HANDLE;
	}

	VulkanPipeline& VulkanPipeline::operator=(VulkanPipeline&& Other) noexcept
	{
		if (this != &Other)
		{
			m_VkPipelineLayout = Other.m_VkPipelineLayout;
			m_VkPipeline = Other.m_VkPipeline;

			Other.m_VkPipelineLayout = VK_NULL_HANDLE;
			Other.m_VkPipeline = VK_NULL_HANDLE;
		}
		return *this;
	}

	VkFormat VulkanPipeline::GetVertexAttributeFormat(uint32 uComponentCount)
	{
		switch (uComponentCount)
		{
		case 1: return VK_FORMAT_R32_SFLOAT;
		case 2: return VK_FORMAT_R32G32_SFLOAT;
		case 3: return VK_FORMAT_R32G32B32_SFLOAT;
		case 4: return VK_FORMAT_R32G32B32A32_SFLOAT;
		default: return VK_FORMAT_UNDEFINED;
		}
	}

	VkPrimitiveTopology VulkanPipeline::GetPrimitiveTopology(RhiPrimitiveTopology eTopology)
	{
		switch (eTopology)
		{
		case RhiPrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
		case RhiPrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		case RhiPrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
		case RhiPrimitiveTopology::TriangleList:
		default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		}
	}

	VkCullModeFlags VulkanPipeline::GetCullMode(RhiCullMode eCullMode)
	{
		switch (eCullMode)
		{
		case RhiCullMode::Front: return VK_CULL_MODE_FRONT_BIT;
		case RhiCullMode::Back: return VK_CULL_MODE_BACK_BIT;
		case RhiCullMode::None:
		default: return VK_CULL_MODE_NONE;
		}
	}

	VkCompareOp VulkanPipeline::GetDepthCompareOperation(RhiCompareOperation eCompare)
	{
		switch (eCompare)
		{
		case RhiCompareOperation::Never: return VK_COMPARE_OP_NEVER;
		case RhiCompareOperation::Less: return VK_COMPARE_OP_LESS;
		case RhiCompareOperation::Equal: return VK_COMPARE_OP_EQUAL;
		case RhiCompareOperation::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
		case RhiCompareOperation::Greater: return VK_COMPARE_OP_GREATER;
		case RhiCompareOperation::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
		case RhiCompareOperation::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
		case RhiCompareOperation::Always: return VK_COMPARE_OP_ALWAYS;
		default: return VK_COMPARE_OP_LESS_OR_EQUAL;
		}
	}

	bool VulkanPipeline::Create(
		const VulkanContext& context,
		VkRenderPass renderPass,
		VkDescriptorSetLayout descriptorSetLayout,
		const PipelineShaderStage& vertexStage,
		const PipelineShaderStage& fragmentStage,
		const RhiGraphicsPipelineState& state)
	{
		if (IsValid())
		{
			LOG_ERROR(kLogTag, "VulkanPipeline is already created.");
			return false;
		}
		if (vertexStage.VkShader == VK_NULL_HANDLE || fragmentStage.VkShader == VK_NULL_HANDLE ||
			vertexStage.pEntryPointName == nullptr || fragmentStage.pEntryPointName == nullptr ||
			renderPass == VK_NULL_HANDLE || descriptorSetLayout == VK_NULL_HANDLE)
		{
			LOG_ERROR(kLogTag, "A graphics pipeline needs both shader stages (module and entry point), a render pass and a descriptor set layout.");
			return false;
		}

		const VkDevice device = context.GetDevice();

		// Push constants: one shared range covering both shader stages. Vulkan
		// requires a 4-byte aligned, non-empty range.
		VkDeviceSize nPushConstantByteSize = state.uPushConstantByteCount;
		if (nPushConstantByteSize < sizeof(uint32))
		{
			nPushConstantByteSize = sizeof(uint32);
		}
		nPushConstantByteSize = (nPushConstantByteSize + 3u) & ~static_cast<VkDeviceSize>(3u);

		VkPushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = static_cast<uint32>(nPushConstantByteSize);

		VkPipelineLayoutCreateInfo layoutCreateInfo = {};
		layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutCreateInfo.setLayoutCount = 1;
		layoutCreateInfo.pSetLayouts = &descriptorSetLayout;
		layoutCreateInfo.pushConstantRangeCount = 1;
		layoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		if (vkCreatePipelineLayout(device, &layoutCreateInfo, nullptr, &m_VkPipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreatePipelineLayout failed.");
			return false;
		}

		if (!CreateGraphicsPipeline(
			device, renderPass, vertexStage, fragmentStage, m_VkPipelineLayout, state, m_VkPipeline))
		{
			vkDestroyPipelineLayout(device, m_VkPipelineLayout, nullptr);
			m_VkPipelineLayout = VK_NULL_HANDLE;
			return false;
		}
		return true;
	}

	bool VulkanPipeline::CreateGraphicsPipeline(
		VkDevice device,
		VkRenderPass renderPass,
		const PipelineShaderStage& vertexStage,
		const PipelineShaderStage& fragmentStage,
		VkPipelineLayout pipelineLayout,
		const RhiGraphicsPipelineState& state,
		VkPipeline& outPipeline)
	{
		// Each stage runs the entry point the shader module was compiled from,
		// which is why the name travels with the module.
		VkPipelineShaderStageCreateInfo shaderStages[2] = {};
		shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		shaderStages[0].module = vertexStage.VkShader;
		shaderStages[0].pName = vertexStage.pEntryPointName;

		shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderStages[1].module = fragmentStage.VkShader;
		shaderStages[1].pName = fragmentStage.pEntryPointName;

		// Vertex input comes from the state the managed render pipeline built.
		VkVertexInputBindingDescription vertexBinding = {};
		vertexBinding.binding = 0;
		vertexBinding.stride = state.uVertexStride;
		vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription vertexAttributes[k_nMaxPipelineVertexAttributeCount] = {};
		for (uint32 uAttributeIndex = 0; uAttributeIndex < state.uVertexAttributeCount; ++uAttributeIndex)
		{
			const RhiVertexAttribute& attribute = state.VertexAttributes[uAttributeIndex];
			vertexAttributes[uAttributeIndex].location = static_cast<uint32>(attribute.nShaderLocation);
			vertexAttributes[uAttributeIndex].binding = 0;
			vertexAttributes[uAttributeIndex].format = GetVertexAttributeFormat(attribute.uComponentCount);
			vertexAttributes[uAttributeIndex].offset = attribute.uByteOffset;
		}

		VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &vertexBinding;
		vertexInputInfo.vertexAttributeDescriptionCount = state.uVertexAttributeCount;
		vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = GetPrimitiveTopology(state.eTopology);
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		// Dynamic viewport/scissor keep resize handling trivial.
		VkPipelineViewportStateCreateInfo viewportState = {};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		VkPipelineDynamicStateCreateInfo dynamicState = {};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		const VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		dynamicState.dynamicStateCount = 2;
		dynamicState.pDynamicStates = dynamicStates;

		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = GetCullMode(state.eCullMode);
		rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;

		VkPipelineMultisampleStateCreateInfo multisampling = {};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
		colorBlendAttachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		colorBlendAttachment.blendEnable = state.bBlendEnabled ? VK_TRUE : VK_FALSE;
		colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

		VkPipelineColorBlendStateCreateInfo colorBlending = {};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		// Depth state: a 3D pipeline tests and writes depth (nearer surfaces win,
		// farther ones are thrown away), a 2D pipeline leaves the attachment
		// alone. The render pass always HAS a depth attachment, so the state is
		// valid either way.
		LOG_INFO(kLogTag, "Pipeline state: depthTest={}, depthWrite={}, cull={}, topology={}.",
			state.bDepthTestEnabled ? 1 : 0, state.bDepthWriteEnabled ? 1 : 0,
			static_cast<int32>(state.eCullMode), static_cast<int32>(state.eTopology));

		VkPipelineDepthStencilStateCreateInfo depthStencil = {};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = state.bDepthTestEnabled ? VK_TRUE : VK_FALSE;
		depthStencil.depthWriteEnable = (state.bDepthTestEnabled && state.bDepthWriteEnabled) ? VK_TRUE : VK_FALSE;
		depthStencil.depthCompareOp = GetDepthCompareOperation(state.eDepthCompare);
		depthStencil.depthBoundsTestEnable = VK_FALSE;
		depthStencil.stencilTestEnable = VK_FALSE;

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
		pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCreateInfo.stageCount = 2;
		pipelineCreateInfo.pStages = shaderStages;
		pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
		pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pRasterizationState = &rasterizer;
		pipelineCreateInfo.pMultisampleState = &multisampling;
		pipelineCreateInfo.pDepthStencilState = &depthStencil;
		pipelineCreateInfo.pColorBlendState = &colorBlending;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.layout = pipelineLayout;
		pipelineCreateInfo.renderPass = renderPass;
		pipelineCreateInfo.subpass = 0;
		pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;
		pipelineCreateInfo.basePipelineIndex = -1;

		const VkResult eResult = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &outPipeline);
		if (eResult != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreateGraphicsPipelines failed (VkResult {}).", static_cast<int32>(eResult));
			return false;
		}
		return true;
	}

	void VulkanPipeline::Destroy(const VulkanContext& context)
	{
		const VkDevice device = context.GetDevice();

		if (m_VkPipeline != VK_NULL_HANDLE) { vkDestroyPipeline(device, m_VkPipeline, nullptr); m_VkPipeline = VK_NULL_HANDLE; }
		if (m_VkPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(device, m_VkPipelineLayout, nullptr); m_VkPipelineLayout = VK_NULL_HANDLE; }
	}
}

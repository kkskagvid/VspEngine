#include "RuntimePCH.h"

#include <cstddef>

#include "Core/Logging/Log.h"
#include "Graphics/Vulkan/Shaders/ShaderBinary.h"
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

	bool VulkanPipeline::Create(
		const VulkanContext& context,
		VkRenderPass renderPass,
		VkDescriptorSetLayout descriptorSetLayout)
	{
		if (IsValid())
		{
			LOG_ERROR(kLogTag, "VulkanPipeline is already created.");
			return false;
		}

		const VkDevice device = context.GetDevice();

		// The bindless implementation is the only supported path.
		// Note: the generated header stores the SPIR-V size in uint32 words;
		// vkCreateShaderModule expects bytes.
		VkShaderModule vertexShader = context.CreateShaderModule(
			Shaders::k_TriangleBindless_vertSpv,
			Shaders::k_nTriangleBindless_vertSpvSize * sizeof(uint32));
		VkShaderModule fragmentShader = context.CreateShaderModule(
			Shaders::k_TriangleBindless_fragSpv,
			Shaders::k_nTriangleBindless_fragSpvSize * sizeof(uint32));

		if (vertexShader == VK_NULL_HANDLE || fragmentShader == VK_NULL_HANDLE)
		{
			return false;
		}

		// Push constants: one shared range covering both shader stages (the
		// GLSL blocks use explicit offsets so the layout matches PushConstants).
		VkPushConstantRange pushConstantRange = {};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = k_nPushConstantByteSize;

		VkPipelineLayoutCreateInfo layoutCreateInfo = {};
		layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutCreateInfo.setLayoutCount = 1;
		layoutCreateInfo.pSetLayouts = &descriptorSetLayout;
		layoutCreateInfo.pushConstantRangeCount = 1;
		layoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		if (vkCreatePipelineLayout(device, &layoutCreateInfo, nullptr, &m_VkPipelineLayout) != VK_SUCCESS)
		{
			LOG_ERROR(kLogTag, "vkCreatePipelineLayout failed.");
			vkDestroyShaderModule(device, vertexShader, nullptr);
			vkDestroyShaderModule(device, fragmentShader, nullptr);
			return false;
		}

		const bool bPipelineCreated = CreateGraphicsPipeline(
			device, renderPass, vertexShader, fragmentShader, m_VkPipelineLayout, m_VkPipeline);

		vkDestroyShaderModule(device, vertexShader, nullptr);
		vkDestroyShaderModule(device, fragmentShader, nullptr);
		return bPipelineCreated;
	}

	bool VulkanPipeline::CreateGraphicsPipeline(
		VkDevice device,
		VkRenderPass renderPass,
		VkShaderModule vertexShader,
		VkShaderModule fragmentShader,
		VkPipelineLayout pipelineLayout,
		VkPipeline& outPipeline)
	{
		VkPipelineShaderStageCreateInfo shaderStages[2] = {};
		shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		shaderStages[0].module = vertexShader;
		shaderStages[0].pName = "main";

		shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderStages[1].module = fragmentShader;
		shaderStages[1].pName = "main";

		// Vertex input: one binding, position + color + uv.
		VkVertexInputBindingDescription vertexBinding = {};
		vertexBinding.binding = 0;
		vertexBinding.stride = sizeof(Vertex2D);
		vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription vertexAttributes[3] = {};
		vertexAttributes[0].location = 0;
		vertexAttributes[0].binding = 0;
		vertexAttributes[0].format = VK_FORMAT_R32G32_SFLOAT;
		vertexAttributes[0].offset = offsetof(Vertex2D, fPositionX);

		vertexAttributes[1].location = 1;
		vertexAttributes[1].binding = 0;
		vertexAttributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		vertexAttributes[1].offset = offsetof(Vertex2D, fColorR);

		vertexAttributes[2].location = 2;
		vertexAttributes[2].binding = 0;
		vertexAttributes[2].format = VK_FORMAT_R32G32_SFLOAT;
		vertexAttributes[2].offset = offsetof(Vertex2D, fUvU);

		VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &vertexBinding;
		vertexInputInfo.vertexAttributeDescriptionCount = 3;
		vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes;

		VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
		rasterizer.cullMode = VK_CULL_MODE_NONE;
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
		colorBlendAttachment.blendEnable = VK_TRUE;
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

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
		pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCreateInfo.stageCount = 2;
		pipelineCreateInfo.pStages = shaderStages;
		pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
		pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pRasterizationState = &rasterizer;
		pipelineCreateInfo.pMultisampleState = &multisampling;
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

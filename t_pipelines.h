#pragma once
#include "t_types.h"

class PipelineBuilder {
public:
	std::vector<VkPipelineShaderStageCreateInfo> _shaderStages; //Holds information about the shade rstages
	VkPipelineInputAssemblyStateCreateInfo _inputAssembly; //Settings for primitive assembly (fixed function)
	VkPipelineRasterizationStateCreateInfo _rasterizer; //Settings for rasterization (fixed function)
	VkPipelineColorBlendAttachmentState _colorBlendAttachment; //Color blend and attachment write information
	VkPipelineMultisampleStateCreateInfo _multisampling; //Settings for AA
	VkPipelineLayout _pipelineLayout;
	VkPipelineDepthStencilStateCreateInfo _depthStencil; //Depth testing and stencil information
	VkPipelineRenderingCreateInfo _renderInfo; //Chained on via pNext, configures attachment formats in place of render passes
	VkFormat _colorAttachmentFormat;

	PipelineBuilder() { clear(); }

	void clear();

	VkPipeline build(VkDevice device);

	void setShaders(VkShaderModule vertShader, VkShaderModule fragShader, const char *vertEntry = "main", const char *fragEntry = "main"); //My modification allows for specifying entry points for vert and frag shaders
	void setInputTopology(VkPrimitiveTopology topology);
	void setPolygonMode(VkPolygonMode mode); //Polygon mode configures triangle filling, line filling, or point filling. We can use it to render meshes, wireframes, or points
	void setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace); //Determines things like backface culling

	void setMultisamplingNone();
	void disableBlending();
	void setColorAttachmentFormat(VkFormat format);
	void setDepthFormat(VkFormat format);
	void disableDepthTest();
};

//TSUKIUTIL
//===================================================================================================================
namespace tsukiutil {
	bool loadShaderModule(const char *filePath, VkDevice device, VkShaderModule *outShaderModule);
};
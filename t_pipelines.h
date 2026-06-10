#pragma once
#include "t_types.h"

namespace tsukiutil {
	bool loadShaderModule(const char *filePath, VkDevice device, VkShaderModule *outShaderModule);
};
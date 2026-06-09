#pragma once
#include "t_types.h"

namespace tsukiutil {
	void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout currLayout, VkImageLayout newLayout);
};
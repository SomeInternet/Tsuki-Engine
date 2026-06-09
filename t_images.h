#pragma once
#include "t_types.h"

//Suite of helper functions for transfering images.
namespace tsukiutil {
	void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout currLayout, VkImageLayout newLayout);

	void memcpyImage(VkCommandBuffer commandBuffer, VkImage src, VkImage dst, VkExtent2D srcExtent, VkExtent2D dstExtent);
};
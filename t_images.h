#pragma once
#include "t_types.h"

//Suite of helper functions for transfering images.
namespace tsukiutil {
	void transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout currLayout, VkImageLayout newLayout);

	void copyImage(VkCommandBuffer commandBuffer, VkImage dst, VkImage src, VkExtent2D dstExtent, VkExtent2D srcExtent);
};
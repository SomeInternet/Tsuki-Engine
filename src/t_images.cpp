#include <stdexcept>

#include "t_images.h"
#include "t_initializers.h"

void tsukiutil::transitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkImageLayout currLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier2 imageBarrier{};

    //All commands recorded to this command buffer must complete for this barrier to be passed, in order for any commands after it in the command buffer to execute
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    imageBarrier.pNext = nullptr;
    imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; //Inefficient, but okay for our purposes. TODO: Make more lightweight {?)
    imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; //Inefficient, but okay for our purposes
    imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
    imageBarrier.oldLayout = currLayout;
    imageBarrier.newLayout = newLayout;

    //Access the depth compononent of the image if it's supposed to be a depth attachment, else access the color
    VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange = tsukiinit::tImageSubresourceRange(aspectMask); //Target the part of the image
    imageBarrier.image = image;

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.pNext = nullptr;

    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &imageBarrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void tsukiutil::copyImage(VkCommandBuffer commandBuffer, VkImage dst, VkImage src, VkExtent2D dstExtent, VkExtent2D srcExtent) {
    //VkCmdCopyImage is another way of copying 1 image to another, but VkCmdBlitImage allows copying between images of different sizes and formats
    VkImageBlit2 blit{};
    blit.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
    blit.pNext = nullptr;
    blit.srcOffsets[1].x = srcExtent.width;
    blit.srcOffsets[1].y = srcExtent.height;
    blit.srcOffsets[1].z = 1;
    blit.dstOffsets[1].x = dstExtent.width;
    blit.dstOffsets[1].y = dstExtent.height;
    blit.dstOffsets[1].z = 1;

    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.srcSubresource.mipLevel = 0;

    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;
    blit.dstSubresource.mipLevel = 0;

    VkBlitImageInfo2 blitInfo{};
    blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
    blitInfo.dstImage = dst;
    blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blitInfo.srcImage = src;
    blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blitInfo.filter = VK_FILTER_LINEAR;
    blitInfo.regionCount = 1;
    blitInfo.pRegions = &blit;

    vkCmdBlitImage2(commandBuffer, &blitInfo);
}

void tsukiutil::copyBufferToImage(VkCommandBuffer commandBuffer, VkImage dst, VkBuffer src, VkExtent2D dstExtent, uint32_t srcOffset) {

    //Tightly packed rows
    VkBufferImageCopy region{};
    region.bufferOffset = srcOffset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    //Tells Vulkan about the image side. Vulkan already knows information about the image through te VkImage. So it performs an ""exact"" copy
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { dstExtent.width, dstExtent.height, 1 };
    
    vkCmdCopyBufferToImage(commandBuffer, src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}
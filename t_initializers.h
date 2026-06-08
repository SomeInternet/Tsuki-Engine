#pragma once

#include "t_types.h"

namespace tsukiinit {
	//Helper suite for Vulkan struct creation

    VkDebugUtilsMessengerCreateInfoEXT tDebugUtilsMessengerCreateInfo(PFN_vkDebugUtilsMessengerCallbackEXT debugCallback);

	VkCommandPoolCreateInfo tCommandPoolCreateInfo(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags = 0);
	VkCommandBufferAllocateInfo tCommandBufferAllocateInfo(VkCommandPool pool, uint32_t count = 1);

    VkCommandBufferBeginInfo tCommandBufferBeginInfo(VkCommandBufferUsageFlags flags = 0);
    VkCommandBufferSubmitInfo tCommandBufferSubmitInfo(VkCommandBuffer cmd);

    VkFenceCreateInfo tFenceCreateInfo(VkFenceCreateFlags flags = 0);

    VkSemaphoreCreateInfo tSemaphoreCreateInfo(VkSemaphoreCreateFlags flags = 0);

    VkSubmitInfo2 tSubmitInfo(VkCommandBufferSubmitInfo *cmd, VkSemaphoreSubmitInfo *signalSemaphoreInfo,
        VkSemaphoreSubmitInfo *waitSemaphoreInfo);
    VkPresentInfoKHR tPresentInfo();

    VkRenderingAttachmentInfo tAttachmentInfo(VkImageView view, VkClearValue *clear, VkImageLayout layout /*= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL*/);

    VkRenderingAttachmentInfo tDepthAttachmentInfo(VkImageView view,
        VkImageLayout layout /*= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL*/ );

    VkRenderingInfo tRenderingInfo(VkExtent2D renderExtent, VkRenderingAttachmentInfo *colorAttachment,
        VkRenderingAttachmentInfo *depthAttachment);

    VkImageSubresourceRange tImageSubresourceRange(VkImageAspectFlags aspectMask);

    VkSemaphoreSubmitInfo tSemaphoreSubmitInfo(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore);
    VkDescriptorSetLayoutBinding tDescriptorSetLayoutBinding(VkDescriptorType type, VkShaderStageFlags stageFlags,
        uint32_t binding);
    VkDescriptorSetLayoutCreateInfo tDescriptorSetLayoutCreateInfo(VkDescriptorSetLayoutBinding *bindings,
        uint32_t bindingCount);

    //Helper function for image descriptors
    VkWriteDescriptorSet tWriteDescriptorImage(VkDescriptorType type, VkDescriptorSet dstSet,
        VkDescriptorImageInfo *imageInfo, uint32_t binding);
    VkWriteDescriptorSet tWriteDescriptorBuffer(VkDescriptorType type, VkDescriptorSet dstSet,
        VkDescriptorBufferInfo *bufferInfo, uint32_t binding);
    VkDescriptorBufferInfo tBufferInfo(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range);

    VkImageCreateInfo tImageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);
    VkImageViewCreateInfo tImageViewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);
    VkPipelineLayoutCreateInfo tPipelineLayoutCreateInfo();
    VkPipelineShaderStageCreateInfo tPipelineShaderStageCreateInfo(VkShaderStageFlagBits stage,
        VkShaderModule shaderModule,
        const char *entry = "main");
}
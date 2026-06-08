#include "t_initializers.h"

VkDebugUtilsMessengerCreateInfoEXT tsukiinit::tDebugUtilsMessengerCreateInfo(PFN_vkDebugUtilsMessengerCallbackEXT debugCallback) {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};

    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    //Specify which severities of messages we want to handle
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    //Filters types of messages
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    createInfo.pUserData = nullptr; // Optional

    return createInfo;
}

VkCommandPoolCreateInfo tsukiinit::tCommandPoolCreateInfo(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags /*= 0*/) {
    VkCommandPoolCreateInfo info {}; //0 initialize

    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.pNext = nullptr;
    info.queueFamilyIndex = queueFamilyIndex;
    info.flags = flags;

    return info;
}

VkCommandBufferAllocateInfo tsukiinit::tCommandBufferAllocateInfo(
    VkCommandPool pool, uint32_t count /*= 1*/) {
    VkCommandBufferAllocateInfo info{};

    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    info.pNext = nullptr;
    info.commandPool = pool;
    info.commandBufferCount = count;
    info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; //Can't be invoked by other command buffers
    
    return info;
}

VkCommandBufferBeginInfo tsukiinit::tCommandBufferBeginInfo(VkCommandBufferUsageFlags flags /*= 0*/ ) {
    VkCommandBufferBeginInfo info{};

    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.pNext = nullptr;
    info.pInheritanceInfo = nullptr;
    info.flags = flags;

    return info;
}

VkCommandBufferSubmitInfo tsukiinit::tCommandBufferSubmitInfo(VkCommandBuffer cmd) {
    VkCommandBufferSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    info.pNext = nullptr;
    info.commandBuffer = cmd;
    info.deviceMask = 0;

    return info;
}

VkFenceCreateInfo tsukiinit::tFenceCreateInfo(VkFenceCreateFlags flags /*= 0*/ ) {
    VkFenceCreateInfo info{};

    info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = flags;

    return info;
}

VkSemaphoreCreateInfo tsukiinit::tSemaphoreCreateInfo(VkSemaphoreCreateFlags flags /*= 0*/ ) {
    VkSemaphoreCreateInfo info{};

    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = flags;

    return info;
}

VkSubmitInfo2 tsukiinit::tSubmitInfo(VkCommandBufferSubmitInfo *cmd, VkSemaphoreSubmitInfo *signalSemaphoreInfo,
    VkSemaphoreSubmitInfo *waitSemaphoreInfo) {
    VkSubmitInfo2 info{};

    info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    info.pNext = nullptr;

    //Semaphore that the queue waits on before executing this command buffer
    info.waitSemaphoreInfoCount = (waitSemaphoreInfo == nullptr) ? 0 : 1;
    info.pWaitSemaphoreInfos = waitSemaphoreInfo;

    //Semaphore that the Vulkan queue signals signals upon this command buffer executing
    info.signalSemaphoreInfoCount = (signalSemaphoreInfo == nullptr) ? 0 : 1;
    info.pSignalSemaphoreInfos = signalSemaphoreInfo;;

    info.commandBufferInfoCount = 1;
    info.pCommandBufferInfos = cmd;

    return info;
}

VkPresentInfoKHR tsukiinit::tPresentInfo() {
    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

    return info;
}

//Render target for colors
VkRenderingAttachmentInfo tsukiinit::tAttachmentInfo(VkImageView view, VkClearValue *clear, VkImageLayout layout /*= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL*/ ) {
    VkRenderingAttachmentInfo colorAttachment{};
    
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.pNext = nullptr;
    colorAttachment.imageView = view;
    colorAttachment.imageLayout = layout;
    colorAttachment.loadOp = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    if (clear) {
        colorAttachment.clearValue = *clear;
    }

    return colorAttachment;
}

//Render target for depth testing
VkRenderingAttachmentInfo tsukiinit::tDepthAttachmentInfo(VkImageView view,
    VkImageLayout layout /*= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL*/) {
    VkRenderingAttachmentInfo depthAttachment{};

    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.pNext = nullptr;

    depthAttachment.imageView = view;
    depthAttachment.imageLayout = layout;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil.depth = 0.f;

    return depthAttachment;
}

VkRenderingInfo tsukiinit::tRenderingInfo(VkExtent2D renderExtent, VkRenderingAttachmentInfo *colorAttachment,
    VkRenderingAttachmentInfo *depthAttachment) {
    VkRenderingInfo renderInfo {};

    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.pNext = nullptr;

    //RenderArea bounds both the viewport and the scissors
    renderInfo.renderArea = VkRect2D{ VkOffset2D { 0, 0 }, renderExtent };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = colorAttachment;
    renderInfo.pDepthAttachment = depthAttachment;
    renderInfo.pStencilAttachment = nullptr;

    return renderInfo;
}

//TODO: Review (-)
//Image subresource ranges tell Vulkan to target a part of an "image"
VkImageSubresourceRange tsukiinit::tImageSubresourceRange(VkImageAspectFlags aspectMask) {
    VkImageSubresourceRange subImage{};

    subImage.aspectMask = aspectMask;
    subImage.baseMipLevel = 0;
    subImage.levelCount = VK_REMAINING_MIP_LEVELS;
    subImage.baseArrayLayer = 0;
    subImage.layerCount = VK_REMAINING_ARRAY_LAYERS;

    return subImage;
}

VkSemaphoreSubmitInfo tsukiinit::tSemaphoreSubmitInfo(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore) {
    VkSemaphoreSubmitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    info.pNext = nullptr;
    info.semaphore = semaphore;
    info.stageMask = stageMask;
    info.value = 0;
    return info;
}

VkDescriptorSetLayoutBinding tsukiinit::tDescriptorSetLayoutBinding(VkDescriptorType type, VkShaderStageFlags stageFlags,
    uint32_t binding) {
    VkDescriptorSetLayoutBinding setbind{};

    setbind.binding = binding;
    setbind.descriptorCount = 1;
    setbind.descriptorType = type;
    setbind.pImmutableSamplers = nullptr;
    setbind.stageFlags = stageFlags;

    return setbind;
}

VkDescriptorSetLayoutCreateInfo tsukiinit::tDescriptorSetLayoutCreateInfo(VkDescriptorSetLayoutBinding *bindings,
    uint32_t bindingCount) {
    VkDescriptorSetLayoutCreateInfo info{};

    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.pNext = nullptr;

    info.pBindings = bindings;
    info.bindingCount = bindingCount;
    info.flags = 0;

    return info;
}

//Helper functions to update types of descriptor sets.
VkWriteDescriptorSet tsukiinit::tWriteDescriptorImage(VkDescriptorType type, VkDescriptorSet dstSet,
    VkDescriptorImageInfo *imageInfo, uint32_t binding) {
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = nullptr;

    write.dstBinding = binding;
    write.dstSet = dstSet;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pImageInfo = imageInfo; //Struct containing information about the image to be used.

    return write;
}

VkWriteDescriptorSet tsukiinit::tWriteDescriptorBuffer(VkDescriptorType type, VkDescriptorSet dstSet,
    VkDescriptorBufferInfo *bufferInfo, uint32_t binding) {
    VkWriteDescriptorSet write{};

    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = nullptr;
    write.dstSet = dstSet;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pBufferInfo = bufferInfo; //Struct containing information about the buffer to be used.
    return write;
}
VkDescriptorBufferInfo tsukiinit::tBufferInfo(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range) {
    VkDescriptorBufferInfo info{};

    info.buffer = buffer;
    info.offset = offset;
    info.range = range;
    return info;
}

VkImageCreateInfo tsukiinit::tImageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent) {
    VkImageCreateInfo info{};

    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.pNext = nullptr;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = extent;
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usageFlags;
    return info;
}

//Create an image view, through which we can access the image
VkImageViewCreateInfo tsukiinit::tImageViewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo info{};

    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.pNext = nullptr;
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange.aspectMask = aspectFlags;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    return info;
}

VkPipelineLayoutCreateInfo tsukiinit::tPipelineLayoutCreateInfo() {
    VkPipelineLayoutCreateInfo info{};

    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0;
    info.setLayoutCount = 0;
    info.pSetLayouts = nullptr;
    info.pushConstantRangeCount = 0;
    info.pPushConstantRanges = nullptr;
    return info;
}

VkPipelineShaderStageCreateInfo tsukiinit::tPipelineShaderStageCreateInfo(VkShaderStageFlagBits stage,
    VkShaderModule shaderModule,
    const char *entry) {
    VkPipelineShaderStageCreateInfo info{};

    info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.pNext = nullptr;
    info.stage = stage;
    info.module = shaderModule;
    info.pName = entry;
    return info;
}
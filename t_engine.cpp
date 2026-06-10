#define VMA_IMPLEMENTATION

#include <chrono>
#include <thread>
#include <iostream>
#include <set>

#include "t_engine.h"
#include "t_initializers.h"
#include "t_types.h"
#include "t_images.h"
#include "t_pipelines.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#if _DEBUG
constexpr bool enableValidationLayers = true;
#else
constexpr bool enableValidationLayers = false;
#endif

const std::vector<const char *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

//Device extensions we want to enable (Swapchains aren't actually in Vulkan core!)
const std::vector<const char *> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, 
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, 
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, 
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME, 
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
        VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
};

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData) {

    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        //The pCallbackData parameter refers to a VkDebugUtilsMessengerCallbackDataEXT 
        //struct containing the details of the message itself, with the most important members being:
        //pMessage: The debug message as a null - terminated string
        //pObjects : Array of Vulkan object handles related to the message
        //objectCount : Number of objects in array

        std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl; //TODO: Replace w/ FMT (?)
    }

    return VK_FALSE;
}

TsukiEngine *loadedEngine = nullptr;

TsukiEngine &TsukiEngine::Get() { return *loadedEngine; }

//MAIN FUNCTIONS
//===================================================================================================================
void TsukiEngine::init() {
	assert(loadedEngine == nullptr);
	loadedEngine = this;

	//Create a GLFW window
    glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); //GLFW was initially created for OpenGL contexts, so we tell it not to create an OpenGL context.
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
	window = glfwCreateWindow( WIDTH, HEIGHT, "Tsuki Engine", nullptr, nullptr);

	//Initialize Vulkan
	initVulkan();
	initSwapChain();
	initCommands();
	initSyncStructures();

    initDescriptors();

    initPipelines();

    initImgui();

	_isInit = true;
}

void TsukiEngine::cleanup() {
	if (_isInit) {
        vkDeviceWaitIdle(_device);

        //TODO: Destroy synchronization structures

        for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
            vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);

            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_device, _frames[i]._swapChainSemaphore, nullptr);

            _frames[i]._deletionQueue.flush();
        }

        _mainDeletionQueue.flush();

        for (int i = 0; i < _swapChainImages.size(); ++i) {
            vkDestroySemaphore(_device, _renderSemaphores[i], nullptr);
        }

        destroySwapChain();
        
        vkDestroySurfaceKHR(_instance, _surface, nullptr);
        vkDestroyDevice(_device, nullptr);
        if (enableValidationLayers) {
            DestroyDebugUtilsMessengerEXT(_instance, _debugMessenger, nullptr);
        }
        vkDestroyInstance(_instance, nullptr);

		glfwDestroyWindow(window);
		glfwTerminate();
	}

	loadedEngine = nullptr;
}

void TsukiEngine::draw() {
	//GENERAL STRUCTURE (AS OF CHAPTER 2)
    //Wait for current frame to finish rendering
    //Grab a swapchain image
    //Begin recording
    //Transition image layouts for the compute pass
    //Dispatch the compute pass
    //Transition image layouts for the image copy (the abstraction we use enforces sequential execution via the image barrier)
    //Copy the compute image to the swapchain image
    //Begin the ImGUI rasterization pass
    //Render ImGUI
    //End the ImGUI rasterization pass
    //Transition the swapchain image for presentation
    //Present it

    VK_CHECK(vkWaitForFences(_device, 1, &getCurrFrame()._renderFence, true, 1000000000)); //Wait for 1 fence (the fence of the current frame) for up to 1 second

    //Clear frame-specific data
    getCurrFrame()._deletionQueue.flush();

    //Get image from swapchain
    uint32_t swapChainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(_device, _swapChain, 1000000000, getCurrFrame()._swapChainSemaphore, nullptr, &swapChainImageIndex)); //Signals the swapchain semaphore when complete

    VK_CHECK(vkResetFences(_device, 1, &getCurrFrame()._renderFence)); //Reset the fence

    _drawExtent.width = _drawImage.imageExtent.width;
    _drawExtent.height = _drawImage.imageExtent.height;

    //Begin command buffer
    VkCommandBuffer commandBuffer = getCurrFrame()._mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));
    VkCommandBufferBeginInfo commandBufferBeginInfo = tsukiinit::tCommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); //1-time command buffer (we re-record each frame)
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

    //Transition image to writeable format
    tsukiutil::transitionImageLayout(commandBuffer, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    //Compute shader
    drawBackground(commandBuffer, swapChainImageIndex);

    //Transition the draw image and the swapchain image to execute copy
    //Recall that within these helpers, we set a barrier that prevents further execution until the instructions before (compute pass) is finished
    tsukiutil::transitionImageLayout(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    tsukiutil::transitionImageLayout(commandBuffer, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    tsukiutil::copyImage(commandBuffer, _swapChainImages[swapChainImageIndex], _drawImage.image, _swapChainExtent, _drawExtent);

    //Transition swapchain image so IMGUI can draw to it
    tsukiutil::transitionImageLayout(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    drawImgui(commandBuffer, _swapChainImageViews[swapChainImageIndex]);

    //Transition swapchain image to presentable format
    tsukiutil::transitionImageLayout(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    //End command buffer
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    //Submit the command buffer to the queue
    VkCommandBufferSubmitInfo commandBufferSubmitInfo = tsukiinit::tCommandBufferSubmitInfo(commandBuffer);
    VkSemaphoreSubmitInfo waitSemaphoreSubmitInfo = tsukiinit::tSemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
        getCurrFrame()._swapChainSemaphore); //Wait on the swap chain semaphore to begin writing to the image (when we receive the image, it's safe to write to)
    VkSemaphoreSubmitInfo signalSemaphoreSubmitInfo = tsukiinit::tSemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, 
        _renderSemaphores[swapChainImageIndex]); //Signal that rendering is complete afterwards (we can now present)
    VkSubmitInfo2 submit = tsukiinit::tSubmitInfo(&commandBufferSubmitInfo, &signalSemaphoreSubmitInfo, &waitSemaphoreSubmitInfo);
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, getCurrFrame()._renderFence));

    //TODO: Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &_swapChain;
    presentInfo.pWaitSemaphores = &_renderSemaphores[swapChainImageIndex];
    presentInfo.waitSemaphoreCount = 1;

    presentInfo.pImageIndices = &swapChainImageIndex;

    VK_CHECK(vkQueuePresentKHR(_graphicsQueue, &presentInfo));
    ++_frameNum;
}

void TsukiEngine::run() {
    //return;
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();

        //TODO: Make sure events process correctly
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        //ImGui::ShowDemoWindow();

        if (ImGui::Begin("background")) {

            ComputeEffect &selected = backgroundEffects[currBackgroundEffect];

            ImGui::Text("Selected effect: ", selected.name);

            ImGui::SliderInt("Effect Index", &currBackgroundEffect, 0, backgroundEffects.size() - 1);

            ImGui::InputFloat4("data1", (float *)&selected.data.data1);
            ImGui::InputFloat4("data2", (float *)&selected.data.data2);
            ImGui::InputFloat4("data3", (float *)&selected.data.data3);
            ImGui::InputFloat4("data4", (float *)&selected.data.data4);
        }
        ImGui::End();

        ImGui::Render();

		draw();
	}
}

//PUBLIC HELPERS
//===================================================================================================================
void TsukiEngine::drawBackground(VkCommandBuffer commandBuffer, uint32_t swapChainImageIndex) {
    //Clear the image color
    //VkClearColorValue clearValue;
    //float flash = std::abs(std::sin(_frameNum / 120.f));
    //clearValue = { { 0.0f, 0.0f, flash, 1.0f } };
    //VkImageSubresourceRange clearRange = tsukiinit::tImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
    //vkCmdClearColorImage(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

    //Pick the pipeline to bind based on the selected effect in IMGUI
    ComputeEffect &effect = backgroundEffects[currBackgroundEffect];

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);
    //Bind the descriptor set with the draw image for the compute pass
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _gradientPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);

    /*ComputePushConstants pushConstants;
    pushConstants.data1 = glm::vec4(1, 0, 0, 1);
    pushConstants.data2 = glm::vec4(0, 0, 1, 1);*/

    vkCmdPushConstants(commandBuffer, _gradientPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &effect.data);

    //Dispatch
    vkCmdDispatch(commandBuffer, std::ceil(_drawExtent.width / 16.f), std::ceil(_drawExtent.height / 16.f), 1); //Like a kernel launch
}

void TsukiEngine::immediateSubmit(std::function<void(VkCommandBuffer commandBuffer)> &&function) {
    VK_CHECK(vkResetFences(_device, 1, &_immFence));
    VK_CHECK(vkResetCommandBuffer(_immCommandBuffer, 0));

    VkCommandBuffer commandBuffer = _immCommandBuffer;

    VkCommandBufferBeginInfo cmdBeginInfo = tsukiinit::tCommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &cmdBeginInfo));

    function(commandBuffer);

    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkCommandBufferSubmitInfo cmdinfo = tsukiinit::tCommandBufferSubmitInfo(commandBuffer);
    VkSubmitInfo2 submit = tsukiinit::tSubmitInfo(&cmdinfo, nullptr, nullptr);

    //_renderFence will now block until the graphics commands finish execution
    VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));
    VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));
}

//PRIVATE HELPERS
//===================================================================================================================
void TsukiEngine::initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();

    //Initialize the VMA
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = _physicalDevice;
    allocatorInfo.device = _device;
    allocatorInfo.instance = _instance;
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT; //Allows us to use device pointers
    vmaCreateAllocator(&allocatorInfo, &_allocator);
    _mainDeletionQueue.push([&]() { vmaDestroyAllocator(_allocator); }); //Push a lambda that when called will destroy the allocator
}

void TsukiEngine::initSwapChain() { //NOTE: Watch out for window resizes later...
    createSwapChain(WIDTH, HEIGHT);

    //
    VkExtent3D drawImageExtent = { _windowExtent.width, _windowExtent.height, 1 };
    _drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    _drawImage.imageExtent = drawImageExtent;

    VkImageUsageFlags drawImageUsages = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    VkImageCreateInfo rImageInfo = tsukiinit::tImageCreateInfo(_drawImage.imageFormat, drawImageUsages, drawImageExtent);

    VmaAllocationCreateInfo rImageAllocationInfo{};
    rImageAllocationInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    rImageAllocationInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vmaCreateImage(_allocator, &rImageInfo, &rImageAllocationInfo, &_drawImage.image, &_drawImage.allocation, nullptr);

    VkImageViewCreateInfo rImageViewInfo = tsukiinit::tImageViewCreateInfo(_drawImage.imageFormat, _drawImage.image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(_device, &rImageViewInfo, nullptr, &_drawImage.imageView));

    //Ensure deletion of the image and image view by adding it to the deletion queue
    _mainDeletionQueue.push([=]() {
        vkDestroyImageView(_device, _drawImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);
        });
}

void TsukiEngine::initCommands() {
    VkCommandPoolCreateInfo commandPoolInfo{};

    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.pNext = nullptr;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolInfo.queueFamilyIndex = _graphicsQueueFamily;

    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_frames[i]._commandPool));

        VkCommandBufferAllocateInfo commandBufferAllocInfo = tsukiinit::tCommandBufferAllocateInfo(_frames[i]._commandPool, 1);
    
        VK_CHECK(vkAllocateCommandBuffers(_device, &commandBufferAllocInfo, &_frames[i]._mainCommandBuffer));
    }

    //Initialize immediate submit
    VK_CHECK(vkCreateCommandPool(_device, &commandPoolInfo, nullptr, &_immCommandPool));

    VkCommandBufferAllocateInfo immCommandBufferAllocInfo = tsukiinit::tCommandBufferAllocateInfo(_immCommandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(_device, &immCommandBufferAllocInfo, &_immCommandBuffer));

    _mainDeletionQueue.push([=]() {vkDestroyCommandPool(_device, _immCommandPool, nullptr); });

}

void TsukiEngine::initSyncStructures() {
    VkFenceCreateInfo fenceCreateInfo = tsukiinit::tFenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT); //Create as pre-signalled, so we don't block immediately
    VkSemaphoreCreateInfo semaphoreCreateInfo = tsukiinit::tSemaphoreCreateInfo();

    for (int i = 0; i < _swapChainImages.size(); ++i) {
        VkSemaphore semaphore;
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &semaphore));
        _renderSemaphores.push_back(semaphore);
    }

    //Create a swapchain semaphore for each image in the swapchain, not each frame in flight
    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapChainSemaphore));

        VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));
    }

    //Initialize immediate submit
    VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));

    _mainDeletionQueue.push([=]() {vkDestroyFence(_device, _immFence, nullptr); });
}

void TsukiEngine::createSwapChain(uint32_t width, uint32_t height) {
    //We need:
        //1) basic surface capabilities (min/max images in swap chain, min/max width and height of images, etc.)
        //2) surface formats
        //3) presentation modes

    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(_physicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = VkExtent2D(width, height);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1; //It's considered good practice to request one more than the minimum
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) { //Make sure we don't exceed the maximum
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = _surface; //Connect our swap chain with our surface (window)
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; //We want to render directly to the images in the swap chain

    QueueFamilyIndices indices = findQueueFamilies(_physicalDevice);
    uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT; //Avoid explicit ownership transfers
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; //images owned by 1 queue family at a time, transfer must be explicit
        createInfo.queueFamilyIndexCount = 0; // Optional
        createInfo.pQueueFamilyIndices = nullptr; // Optional
    }
    createInfo.preTransform = swapChainSupport.capabilities.currentTransform; //No transformation
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    _swapChainImageFormat = surfaceFormat.format;
    _swapChainExtent = extent;

    VK_CHECK(vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_swapChain));

    vkGetSwapchainImagesKHR(_device, _swapChain, &imageCount, nullptr);
    _swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(_device, _swapChain, &imageCount, _swapChainImages.data());

    //Create image views of the swap chain images
    for (int i = 0; i < _swapChainImages.size(); ++i) {
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.pNext = nullptr;
        info.image = _swapChainImages[i];
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = surfaceFormat.format;
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = 1;

        VkImageView view;
        VK_CHECK(vkCreateImageView(_device, &info, nullptr, &view));
        _swapChainImageViews.push_back(view);
    }
}

void TsukiEngine::destroySwapChain() {
    vkDestroySwapchainKHR(_device, _swapChain, nullptr);
    for (int i = 0; i < _swapChainImageViews.size(); ++i) { //TODO: Optimize?
        vkDestroyImageView(_device, _swapChainImageViews[i], nullptr);
    }
}

void TsukiEngine::initDescriptors() {
    std::vector<DescriptorAllocator::PoolSizeRatio> sizes = { {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1} }; //1 storage image (image writeable to from compute shader) per descriptor set
    globalDescriptorAllocator.initPool(_device, 10, sizes); //Max 10 descriptor sets

    DescriptorLayoutBuilder builder;
    builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE); //Promise an image at binding 0
    _drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);

    _drawImageDescriptors = globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout); //Allocate 1 image

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.imageView = _drawImage.imageView;

    VkWriteDescriptorSet drawImageWrite{};
    drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;;
    drawImageWrite.pNext = nullptr;

    //Create a write pointing to the binding (0) and our image (via the image view)
    drawImageWrite.dstBinding = 0;
    drawImageWrite.dstSet = _drawImageDescriptors;
    drawImageWrite.descriptorCount = 1;
    drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    drawImageWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(_device, 1, &drawImageWrite, 0, nullptr);

    _mainDeletionQueue.push([&]() {
        globalDescriptorAllocator.destroyPool(_device);
        vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
        });
}

void TsukiEngine::initPipelines() {
    initBackgroundPipelines();
}

void TsukiEngine::initBackgroundPipelines() { //TODO: Just copy this over to initPipelines (?)
    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;
    computeLayout.setLayoutCount = 1;
    computeLayout.pSetLayouts = &_drawImageDescriptorLayout;

    VkPushConstantRange pushConstant{};
    pushConstant.offset = 0;
    pushConstant.size = sizeof(ComputePushConstants);
    pushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    computeLayout.pPushConstantRanges = &pushConstant;
    computeLayout.pushConstantRangeCount = 1;

    VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_gradientPipelineLayout));

    VkShaderModule computeGradientShader;
    if (!tsukiutil::loadShaderModule("./Shaders/gradient.spv", _device, &computeGradientShader)) {
        std::cerr << "Error building compute shader" << std::endl;
    }

    VkShaderModule computeColorBlendShader;
    if (!tsukiutil::loadShaderModule("./Shaders/colorblend.spv", _device, &computeColorBlendShader)) {
        std::cerr << "Error building compute shader" << std::endl;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.pNext = nullptr;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = computeGradientShader;
    stageInfo.pName = "main"; //Entry point (?)

    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = _gradientPipelineLayout;
    computePipelineCreateInfo.stage = stageInfo;

    ComputeEffect gradient;
    gradient.layout = _gradientPipelineLayout;
    gradient.name = "gradient";
    gradient.data = {};

    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient.pipeline));

    stageInfo.module = computeColorBlendShader;
    computePipelineCreateInfo.stage = stageInfo; //Was copied by value, so I have to reassign the modified version

    ComputeEffect colorBlend;
    colorBlend.layout = _gradientPipelineLayout;
    colorBlend.name = "color blend";
    colorBlend.data = {};

    colorBlend.data.data1 = glm::vec4(1, 0, 0, 1);
    colorBlend.data.data2 = glm::vec4(0, 0, 1, 1);

    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &colorBlend.pipeline));

    backgroundEffects.push_back(gradient);
    backgroundEffects.push_back(colorBlend);

    vkDestroyShaderModule(_device, computeGradientShader, nullptr);
    vkDestroyShaderModule(_device, computeColorBlendShader, nullptr);

    _mainDeletionQueue.push([=]() {
        vkDestroyPipelineLayout(_device, _gradientPipelineLayout, nullptr);
        vkDestroyPipeline(_device, colorBlend.pipeline, nullptr);
        vkDestroyPipeline(_device, gradient.pipeline, nullptr);
        });
}

void TsukiEngine::initImgui() { //TODO
    //Create descriptor pool for IMGUI
    //TODO: Optimize (?)
    VkDescriptorPoolSize poolSizes[] = { { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 } };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; //TODO: Check out what this means
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;
    
    VkDescriptorPool imguiPool;
    VK_CHECK(vkCreateDescriptorPool(_device, &poolInfo, nullptr, &imguiPool));

    //Initialize IMGUI
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(window, true);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = _instance;
    initInfo.PhysicalDevice = _physicalDevice;
    initInfo.Device = _device;
    initInfo.Queue = _graphicsQueue;
    initInfo.DescriptorPool = imguiPool;
    initInfo.MinImageCount = 3;
    initInfo.ImageCount = 3;
    initInfo.UseDynamicRendering = true;

    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = VkPipelineRenderingCreateInfoKHR{};
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_swapChainImageFormat; //We draw IMGUI directly into the swapchain images
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);
    //ImGui_ImplVulkan_CreateFontsTexture();

    //Add to destroy queue
    _mainDeletionQueue.push([=]() {
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(_device, imguiPool, nullptr);
        });
}

void TsukiEngine::drawImgui(VkCommandBuffer commandBuffer, VkImageView targetImageView) {
    //Set the clear value pointer to nullptr so we load in the compute image as-is
    VkRenderingAttachmentInfo colorAttachmentInfo = tsukiinit::tRenderingAttachmentInfo(targetImageView, nullptr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    VkRenderingInfo renderingInfo = tsukiinit::tRenderingInfo(_swapChainExtent, &colorAttachmentInfo, nullptr);

    vkCmdBeginRendering(commandBuffer, &renderingInfo); //Activate dynamic rendering
    //vkCmdBeginRendering and vkCmdEndRendering explicitly tell Vulkan that we're entering a rasterization state
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer); //Records IMGUI stuff to be drawn
    vkCmdEndRendering(commandBuffer);
}

//VULKAN SETUP (ADAPTED FROM VULKAN-TUTORIAL)
//===================================================================================================================
void TsukiEngine::createInstance() {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Tsuki Engine Demo";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Tsuki Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_4;

    auto extensions = getInstanceExtensions();

    //Instance extensions configure Vulkan's capabilities apart from any graphics card
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        debugCreateInfo = tsukiinit::tDebugUtilsMessengerCreateInfo(debugCallback);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
    }
    else {
        createInfo.enabledLayerCount = 0;

        createInfo.pNext = nullptr;
    }

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &_instance));
}

void TsukiEngine::setupDebugMessenger() {
    if (!enableValidationLayers) { return; }

    VkDebugUtilsMessengerCreateInfoEXT createInfo = tsukiinit::tDebugUtilsMessengerCreateInfo(debugCallback);

    VK_CHECK(createDebugUtilsMessengerEXT(_instance, &createInfo, nullptr, &_debugMessenger));
}

void TsukiEngine::createSurface() {
    VK_CHECK(glfwCreateWindowSurface(_instance, window, nullptr, &_surface));
}

void TsukiEngine::pickPhysicalDevice() {
    _physicalDevice = VK_NULL_HANDLE;
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());
    for (const auto &device : devices) {
        if (isDeviceSuitable(device)) {
            _physicalDevice = device;
            break;
        }
    }

    if (_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find suitable GPU!");
    }
}

void TsukiEngine::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(_physicalDevice);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    //Device features
    VkPhysicalDeviceFeatures2 features2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
    features2.features.samplerAnisotropy = VK_TRUE;

    VkPhysicalDeviceVulkan11Features vk11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    vk11.shaderDrawParameters = VK_TRUE;

    VkPhysicalDeviceVulkan12Features vk12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    vk12.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceVulkan13Features vk13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    vk13.synchronization2 = VK_TRUE;
    vk13.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT };
    eds.extendedDynamicState = VK_TRUE;

    features2.pNext = &vk11;
    vk11.pNext = &vk12;
    vk12.pNext = &vk13;
    vk13.pNext = &eds;

    //Device creation
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pNext = &features2;
    createInfo.pEnabledFeatures = nullptr;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    //For older Vulkan implementations (newer implementations ignore this,
    //as there isn't a distinction between instance and device validation layers anymore)
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    }
    else {
        createInfo.enabledLayerCount = 0;
    }

    VK_CHECK(vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device));

    //TODO: Create queues
    vkGetDeviceQueue(_device, indices.graphicsFamily.value(), 0, &_graphicsQueue);
    _graphicsQueueFamily = indices.graphicsFamily.value();
    //vkGetDeviceQueue(_device, indices.presentFamily.value(), 0, &presentQueue);
}

//VULKAN SETUP HELPERS
//===================================================================================================================
bool TsukiEngine::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    //Iterate through the requested validation layers
    for (const char* layerName : validationLayers) {
        bool layerFound = false;

		//Iterate through the available validation layers to check if the requested one is present
        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

std::vector<const char *> TsukiEngine::getInstanceExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char *> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    extensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
    extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);

    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

VkResult TsukiEngine::createDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void TsukiEngine::DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

bool TsukiEngine::isDeviceSuitable(VkPhysicalDevice device) { //TODO: Update
    //We need to check:
    //1) Supports Vulkan 1.4
    //2) Supports graphics and present queue families
    //3) Supports our required device extensions
    //4) Supports our required features

    QueueFamilyIndices indices = findQueueFamilies(device);

    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

    VkPhysicalDeviceFeatures2 supportedFeatures2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };

    //TODO: Learn more about these
    VkPhysicalDeviceVulkan11Features vk11 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
    VkPhysicalDeviceVulkan12Features vk12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
    VkPhysicalDeviceVulkan13Features vk13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT };

    supportedFeatures2.pNext = &vk11;
    vk11.pNext = &vk12;
    vk12.pNext = &vk13;
    vk13.pNext = &eds;

    vkGetPhysicalDeviceFeatures2(device, &supportedFeatures2);

    bool supportsRequiredFeatures =
        supportedFeatures2.features.samplerAnisotropy &&
        vk11.shaderDrawParameters &&
        vk12.bufferDeviceAddress &&
        vk13.synchronization2 &&
        vk13.dynamicRendering &&
        eds.extendedDynamicState;

    return indices.isComplete() && extensionsSupported && swapChainAdequate && supportsRequiredFeatures;
}

bool TsukiEngine::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto &extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

QueueFamilyIndices TsukiEngine::findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    //Retrieve the queue families supported by this device
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const VkQueueFamilyProperties queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) { //set graphicsFamily to the index of the queue family that supports VK_QUEUE_GRAPHICS_BIT
            indices.graphicsFamily = i;
        }
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, _surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }

        ++i;
    }

    return indices;
}

SwapChainSupportDetails TsukiEngine::querySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, _surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, _surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, _surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

VkSurfaceFormatKHR TsukiEngine::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats) {
    for (const auto &availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR TsukiEngine::chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes) {
    for (const auto &availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }

    //FIFO is the only present mode guaranteed to be available
    return VK_PRESENT_MODE_FIFO_KHR;
}
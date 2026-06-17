#define VMA_IMPLEMENTATION

#include <chrono>
#include <thread>
#include <iostream>
#include <set>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

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

//MATERIALS
//===================================================================================================================
void GLTFMetallicRoughness::buildPipelines(TsukiEngine *engine) {
    VkShaderModule meshShader;
    if (!tsukiutil::loadShaderModule("./Shaders/gradient.spv", engine->_device, &meshShader)) {
        std::cerr << "Error building mesh shader" << std::endl;
    }

    VkPushConstantRange matrixRange{};
    matrixRange.offset = 0;
    matrixRange.size = sizeof(GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    //Create our descriptor set layout
    DescriptorLayoutBuilder layoutBuilder;
    layoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    layoutBuilder.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    layoutBuilder.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    materialLayout = layoutBuilder.build(engine->_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

    VkDescriptorSetLayout layouts[] = { engine->_sceneDataDescriptorLayout, materialLayout };

    //Create our pipeline layout
    VkPipelineLayoutCreateInfo meshPipelineLayoutInfo = tsukiinit::tPipelineLayoutCreateInfo();
    meshPipelineLayoutInfo.setLayoutCount = 2;
    meshPipelineLayoutInfo.pSetLayouts = layouts;
    meshPipelineLayoutInfo.pushConstantRangeCount = 1;
    meshPipelineLayoutInfo.pPushConstantRanges = &matrixRange;
    
    VkPipelineLayout newPipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(engine->_device, &meshPipelineLayoutInfo, nullptr, &newPipelineLayout));

    opaquePipeline.layout = newPipelineLayout;
    transparentPipeline.layout = newPipelineLayout;

    PipelineBuilder pipelineBuilder;
    pipelineBuilder.setShaders(meshShader, meshShader, "vertMain", "fragMain");
    pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.setMultisamplingNone();
    pipelineBuilder.disableBlending();
    pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    pipelineBuilder.setColorAttachmentFormat(engine->_drawImage.imageFormat);
    pipelineBuilder.setDepthFormat(engine->_depthImage.imageFormat);

    opaquePipeline.pipeline = pipelineBuilder.build(engine->_device);

    //Enable blending for transparent objects
    pipelineBuilder.enableBlendingAdditive();
    pipelineBuilder.enableDepthTest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);
    transparentPipeline.pipeline = pipelineBuilder.build(engine->_device);
}
void GLTFMetallicRoughness::clearResources(VkDevice device) {

}

TsukiMaterial GLTFMetallicRoughness::writeMaterial(VkDevice device, TsukiMaterialPass pass, const GLTFMetallicRoughness::MaterialResources &resources,
    DynamicDescriptorAllocator &descriptorAllocator) {
    TsukiMaterial materialData;

    materialData.passType = pass;
    materialData.pipeline = (pass == TsukiMaterialPass::TSUKI_MATERIAL_TRANSPARENT) ? &transparentPipeline : &opaquePipeline;
    materialData.descriptorSet = descriptorAllocator.allocate(device, materialLayout);

    writer.clear();
    writer.writeBuffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.writeImage(1, resources.colorImage.imageView, resources.colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.writeImage(2, resources.metallicRoughnessImage.imageView, resources.metallicRoughnessSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

    writer.updateSet(device, materialData.descriptorSet);

    return materialData;
}

//SCENEGRAPH
//===================================================================================================================
void TMeshNode::Draw(const glm::mat4 &matrix, TsukiDrawContext &context) {
    glm::mat4 nodeMatrix = matrix * worldTransform;

    //Add the submeshes to the draw context to be drawn
    for (auto &subMesh : mesh->subMeshes) {
        TsukiRenderObject def;
        def.size = subMesh.size;
        def.offset = subMesh.offset;

        def.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
        def.material = &subMesh.material->data;
        def.transform = nodeMatrix;
        def.vertexBufferAddress = mesh->meshBuffers.vertexBufferAddress;

        context.opaqueSurfaces.push_back(def);
    }

    //Recurse to the children
    TNode::Draw(matrix, context);
}

//MAIN FUNCTIONS
//===================================================================================================================
TsukiEngine *loadedEngine = nullptr;

TsukiEngine &TsukiEngine::Get() { return *loadedEngine; }

void TsukiEngine::init() {
	assert(loadedEngine == nullptr);
	loadedEngine = this;

	//Create a GLFW window
    glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); //GLFW was initially created for OpenGL contexts, so we tell it not to create an OpenGL context.
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	_window = glfwCreateWindow( WIDTH, HEIGHT, "Tsuki Engine", nullptr, nullptr);

    glfwSetWindowUserPointer(_window, this); //Give the GLFW window a pointer to this instance of the engine

    glfwSetWindowIconifyCallback(_window, [](GLFWwindow *window, int iconified) {
        auto tsukiEngine = reinterpret_cast<TsukiEngine *>(glfwGetWindowUserPointer(window));
        tsukiEngine->_render = !iconified;
        });

	//Initialize Vulkan
	initVulkan();
	initSwapChain();
	initCommands();
	initSyncStructures();

    initDescriptors();

    initPipelines();
    initTrianglePipeline();
    initMeshPipeline();

    initDefaultMeshData();

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

        for (auto &mesh : testMeshes) {
            destroyBuffer(mesh->meshBuffers.indexBuffer);
            destroyBuffer(mesh->meshBuffers.vertexBuffer);
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

		glfwDestroyWindow(_window);
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
    getCurrFrame()._frameDescriptors.destroyPools(_device);

    //Get image from swapchain
    uint32_t swapChainImageIndex; 

    VkResult result = vkAcquireNextImageKHR(_device, _swapChain, 1000000000, getCurrFrame()._swapChainSemaphore, nullptr, &swapChainImageIndex); //Signals the swapchain semaphore when complete
    if (result == VK_ERROR_OUT_OF_DATE_KHR) { //Swapchain is out of date due to needing to be resized
        _resize = true;
        return;
    }

    VK_CHECK(vkResetFences(_device, 1, &getCurrFrame()._renderFence)); //Reset the fence

    _drawExtent.width = std::min(_drawImage.imageExtent.width, _swapChainExtent.width) * renderScale;
    _drawExtent.height = std::min(_drawImage.imageExtent.height, _swapChainExtent.height) * renderScale;

    //Begin command buffer
    VkCommandBuffer commandBuffer = getCurrFrame()._mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));
    VkCommandBufferBeginInfo commandBufferBeginInfo = tsukiinit::tCommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); //1-time command buffer (we re-record each frame)
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

    //Transition image to writeable format
    tsukiutil::transitionImageLayout(commandBuffer, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    //Compute shader
    drawBackground(commandBuffer, swapChainImageIndex);

    //Transition image to draw geometry
    tsukiutil::transitionImageLayout(commandBuffer, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL); //Rasterization draws are optimized on color attachment optimal
    //Transition the depth image to a writable format
    tsukiutil::transitionImageLayout(commandBuffer, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    //Draw geometry
    drawGeometry(commandBuffer);

    //Transition the draw image and the swapchain image to execute copy
    //Recall that within these helpers, we set a barrier that prevents further execution until the instructions before (compute pass) is finished
    tsukiutil::transitionImageLayout(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    tsukiutil::transitionImageLayout(commandBuffer, _drawImage.image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

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

    result = vkQueuePresentKHR(_graphicsQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) { 
        _resize = true;
        return;
    }
    ++_frameNum;
}

void TsukiEngine::run() {
    //return;
	while (!glfwWindowShouldClose(_window)) {
		glfwPollEvents();

        //TODO: Make sure events process correctly
        if (_render) {

            //Recreate the swapchain if needed
            if (_resize) { resizeSwapChain(); }

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            if (ImGui::Begin("background")) {
                ImGui::SliderFloat("Render Scale",&renderScale, 0.3f, 1.f);

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

//MESH HELPERS
//===================================================================================================================
GPUMeshBuffers TsukiEngine::uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices) {
    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

    GPUMeshBuffers mesh;

    //Allocate a buffer on the GPU to hold the vertices
    mesh.vertexBuffer = createBuffer(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    //Get the address of the allocated vertex buffer
    VkBufferDeviceAddressInfo deviceAddressInfo{};
    deviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    deviceAddressInfo.buffer = mesh.vertexBuffer.buffer;

    mesh.vertexBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAddressInfo);

    //Allocate a buffer on the GPU to hold the indices
    mesh.indexBuffer = createBuffer(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    AllocatedBuffer stagingBuffer = createBuffer(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    void *data = stagingBuffer.allocation->GetMappedData();
    memcpy(data, vertices.data(), vertexBufferSize);
    memcpy((char *)data + vertexBufferSize, indices.data(), indexBufferSize);

    immediateSubmit([&](VkCommandBuffer commandBuffer) { //NOTE: Perhaps later, push this task to a background thread
        //Copy the data from the staging buffer to the GPU-side vertex buffer
        VkBufferCopy vertexBufferCopy{};
        vertexBufferCopy.dstOffset = 0;
        vertexBufferCopy.srcOffset = 0;;
        vertexBufferCopy.size = vertexBufferSize;

        vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, mesh.vertexBuffer.buffer, 1, &vertexBufferCopy);

        //Do the same with the index buffer
        VkBufferCopy indexBufferCopy{};
        indexBufferCopy.dstOffset = 0;
        indexBufferCopy.srcOffset = vertexBufferSize;
        indexBufferCopy.size = indexBufferSize;

        vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, mesh.indexBuffer.buffer, 1, &indexBufferCopy);
        });

    destroyBuffer(stagingBuffer);

    return mesh;
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

    //Initialize the depth image
    _depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
    _depthImage.imageExtent = drawImageExtent;

    VkImageCreateInfo depthImageInfo = tsukiinit::tImageCreateInfo(_depthImage.imageFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, drawImageExtent);
    vmaCreateImage(_allocator, &depthImageInfo, &rImageAllocationInfo, &_depthImage.image, &_depthImage.allocation, nullptr);

    VkImageViewCreateInfo depthImageViewInfo = tsukiinit::tImageViewCreateInfo(_depthImage.imageFormat, _depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT);

    VK_CHECK(vkCreateImageView(_device, &depthImageViewInfo, nullptr, &_depthImage.imageView));

    //Ensure deletion of the image and image view by adding it to the deletion queue
    _mainDeletionQueue.push([=]() {
        vkDestroyImageView(_device, _drawImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _drawImage.image, _drawImage.allocation);

        vkDestroyImageView(_device, _depthImage.imageView, nullptr);
        vmaDestroyImage(_allocator, _depthImage.image, _depthImage.allocation);
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
    for (int i = 0; i < _swapChainImageViews.size(); ++i) { //TODO: Optimize?
        vkDestroyImageView(_device, _swapChainImageViews[i], nullptr);
    }
    _swapChainImageViews.clear();

    vkDestroySwapchainKHR(_device, _swapChain, nullptr);
}

void TsukiEngine::resizeSwapChain() {
    vkDeviceWaitIdle(_device);
    destroySwapChain();

    int width, height;
    glfwGetFramebufferSize(_window, &width, &height);
    _windowExtent.width = width;
    _windowExtent.height = height;

    createSwapChain(_windowExtent.width, _windowExtent.height);

    _resize = false;
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

    //Create a descriptor set layout that notes a uniform buffer will be available at the vertex and fragment shader stages
    {
        DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        _sceneDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    //Create a descriptor pool for each frame in flight
    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        std::vector<DynamicDescriptorAllocator::PoolSizeRatio> framePoolSizes = {
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4}
        };

        _frames[i]._frameDescriptors = DynamicDescriptorAllocator{};
        _frames[i]._frameDescriptors.init(_device, 1000, framePoolSizes);

        _mainDeletionQueue.push([&, i]() {
            _frames[i]._frameDescriptors.destroyPools(_device);
            });
    }

    {
        DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        _singleImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_FRAGMENT_BIT);
    }
}

void TsukiEngine::initPipelines() {
    initBackgroundPipelines();
}

void TsukiEngine::initBackgroundPipelines() { //TODO: Just copy this over to initPipelines (?)
    VkPipelineLayoutCreateInfo computeLayout{};
    computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    computeLayout.pNext = nullptr;
    computeLayout.setLayoutCount = 1;
    computeLayout.pSetLayouts = &_drawImageDescriptorLayout; //Plug in the descriptor set layout, promising certain kinds of resources

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
    ImGui_ImplGlfw_InitForVulkan(_window, true);

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

//Chapter 3
void TsukiEngine::initTrianglePipeline() {
    VkShaderModule triangleShader;
    if (!tsukiutil::loadShaderModule("./Shaders/triangle.spv", _device, &triangleShader)) {
        std::cerr << "Error building triangle shader" << std::endl;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = tsukiinit::tPipelineLayoutCreateInfo();
    VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_trianglePipelineLayout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _trianglePipelineLayout;
    pipelineBuilder.setShaders(triangleShader, triangleShader, "vertMain", "fragMain");
    pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST); //Draw triangles
    pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.setMultisamplingNone();
    pipelineBuilder.disableBlending();
    pipelineBuilder.disableDepthTest();

    pipelineBuilder.setColorAttachmentFormat(_drawImage.imageFormat);
    pipelineBuilder.setDepthFormat(_depthImage.imageFormat); //We don't have a depth attachment

    _trianglePipeline = pipelineBuilder.build(_device);

    vkDestroyShaderModule(_device, triangleShader, nullptr);

    _mainDeletionQueue.push([&]() {
        vkDestroyPipelineLayout(_device, _trianglePipelineLayout, nullptr);
        vkDestroyPipeline(_device, _trianglePipeline, nullptr);
        });
}

void TsukiEngine::drawGeometry(VkCommandBuffer commandBuffer) {
    VkRenderingAttachmentInfo colorAttachment = tsukiinit::tAttachmentInfo(_drawImage.imageView, nullptr);
    VkRenderingAttachmentInfo depthAttachment = tsukiinit::tDepthAttachmentInfo(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = tsukiinit::tRenderingInfo(_drawExtent, &colorAttachment, &depthAttachment);
    vkCmdBeginRendering(commandBuffer, &renderInfo);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _trianglePipeline);

    //Dynamically set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = _drawExtent.width;
    viewport.height = _drawExtent.height;
    viewport.minDepth = 0;
    viewport.maxDepth = 1;

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport); //0 is the index of the first viewport, 1 viewport

    VkRect2D scissor{};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = _drawExtent.width;
    scissor.extent.height = _drawExtent.height;

    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0); //Draw 3 vertices

    GPUDrawPushConstants pushConstants;
    pushConstants.worldMatrix = glm::mat4{ 1.f };
    pushConstants.vertexBuffer = rectangle.vertexBufferAddress;

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipeline);

    //Allocate a descriptor set for the texture image from our dynamic descriptor set allocator
    VkDescriptorSet imageSet = getCurrFrame()._frameDescriptors.allocate(_device, _singleImageDescriptorLayout);

    { //Scope this
        DescriptorWriter writer;
        writer.writeImage(0, _errorCheckerboardImage.imageView, _defaultSamplerNearest, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        writer.updateSet(_device, imageSet);
    }

    vkCmdPushConstants(commandBuffer, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
    vkCmdBindIndexBuffer(commandBuffer, rectangle.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    //vkCmdDrawIndexed(commandBuffer, 6, 1, 0, 0, 0);

    AllocatedBuffer sceneDataBuffer = createBuffer(sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    getCurrFrame()._deletionQueue.push([=, this]() {
        destroyBuffer(sceneDataBuffer);
        });

    SceneData *sceneDataData = reinterpret_cast<SceneData *>(sceneDataBuffer.allocation->GetMappedData());
    *sceneDataData = sceneData;

    //Create a descriptor set that binds the data and update it
    VkDescriptorSet globalDescriptor = getCurrFrame()._frameDescriptors.allocate(_device, _sceneDataDescriptorLayout);
    {
        DescriptorWriter writer;
        writer.writeBuffer(0, sceneDataBuffer.buffer, sizeof(SceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        writer.updateSet(_device, globalDescriptor);
    }

    //Bind the descriptor set containing our texture
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _meshPipelineLayout, 0, 1, &imageSet, 0, nullptr);

    pushConstants.vertexBuffer = testMeshes[2]->meshBuffers.vertexBufferAddress;
    glm::mat4 view = glm::translate(glm::vec3(0, 0, -5));
    glm::mat4 proj = glm::perspective(glm::radians(70.f), static_cast<float>(_drawExtent.width) / static_cast<float>(_drawExtent.height), .1f, 10000.f);
    proj[1][1] *= -1; //Flip the Y of the projection matrix
    pushConstants.worldMatrix = proj * view;

    vkCmdPushConstants(commandBuffer, _meshPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GPUDrawPushConstants), &pushConstants);
    vkCmdBindIndexBuffer(commandBuffer, testMeshes[2]->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(commandBuffer, testMeshes[2]->subMeshes[0].size, 1, testMeshes[2]->subMeshes[0].offset, 0, 0);
    
    vkCmdEndRendering(commandBuffer);
}

void TsukiEngine::initMeshPipeline() {
    VkShaderModule meshShader;
    if (!tsukiutil::loadShaderModule("./Shaders/buffertest.spv", _device, &meshShader)) {
        std::cerr << "Error building buffertest shader" << std::endl;
    }

    //Set up push constants
    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(GPUDrawPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = tsukiinit::tPipelineLayoutCreateInfo();
    //Include push constants in the pipeline layout
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &bufferRange;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &_singleImageDescriptorLayout;

    VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_meshPipelineLayout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _meshPipelineLayout;
    pipelineBuilder.setShaders(meshShader, meshShader, "vertMain", "fragTextureMain");
    pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST); //Draw meshs
    pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.setMultisamplingNone();
    pipelineBuilder.disableBlending();
    pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_GREATER_OR_EQUAL); //Render fragment iff the current depth image depth is equal to or greater than the fragment depth

    pipelineBuilder.setColorAttachmentFormat(_drawImage.imageFormat);
    pipelineBuilder.setDepthFormat(_depthImage.imageFormat);

    _meshPipeline = pipelineBuilder.build(_device);

    vkDestroyShaderModule(_device, meshShader, nullptr);

    _mainDeletionQueue.push([&]() {
        vkDestroyPipelineLayout(_device, _meshPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _meshPipeline, nullptr);
        });
}

void TsukiEngine::initDefaultMeshData() {
    std::array<Vertex, 4> vertices;

    vertices[0].pos = { 0.5,-0.5, 0 };
    vertices[1].pos = { 0.5,0.5, 0 };
    vertices[2].pos = { -0.5,-0.5, 0 };
    vertices[3].pos = { -0.5,0.5, 0 };

    vertices[0].color = { 0,0, 0,1 };
    vertices[1].color = { 0.5,0.5,0.5 ,1 };
    vertices[2].color = { 1,0, 0,1 };
    vertices[3].color = { 0,1, 0,1 };

    std::array<uint32_t, 6> indices;

    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;

    indices[3] = 2;
    indices[4] = 1;
    indices[5] = 3;

    rectangle = uploadMesh(indices, vertices);

    //delete the rectangle data on engine shutdown
    _mainDeletionQueue.push([&]() {
        destroyBuffer(rectangle.indexBuffer);
        destroyBuffer(rectangle.vertexBuffer);
        });

    uint32_t white = glm::packUnorm4x8(glm::vec4(1));
    _whiteImage = createImage(reinterpret_cast<void *>(&white), VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t gray = glm::packUnorm4x8(glm::vec4(glm::vec3(.67f), 1));
    _grayImage = createImage(reinterpret_cast<void *>(&gray), VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t black = glm::packUnorm4x8(glm::vec4(0));
    _blackImage = createImage(reinterpret_cast<void *>(&black), VkExtent3D{ 1, 1, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT);

    uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
    std::array<uint32_t, 16 * 16> pixels;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black; //^ is bitwise XOR
        }
    }
    _errorCheckerboardImage = createImage(pixels.data(), VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT);

    VkSamplerCreateInfo sampler{};

    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;

    vkCreateSampler(_device, &sampler, nullptr, &_defaultSamplerNearest);

    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(_device, &sampler, nullptr, &_defaultSamplerLinear);

    _mainDeletionQueue.push([&]() {
        vkDestroySampler(_device, _defaultSamplerNearest, nullptr);
        vkDestroySampler(_device, _defaultSamplerLinear, nullptr);

        destroyImage(_whiteImage);
        destroyImage(_grayImage);
        destroyImage(_blackImage);
        destroyImage(_errorCheckerboardImage);
        });

    testMeshes = tsukiutil::loadGltf(this, "./Models/basicmesh.glb").value();

    GLTFMetallicRoughness::MaterialResources materialResources;
    materialResources.colorImage = _whiteImage;
    materialResources.colorSampler = _defaultSamplerLinear;
    materialResources.metallicRoughnessImage = _whiteImage;
    materialResources.metallicRoughnessSampler = _defaultSamplerLinear;

    AllocatedBuffer materialConstants = createBuffer(sizeof(GLTFMetallicRoughness::MaterialConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    //Set the material constants data in the GPU-visible memory
    GLTFMetallicRoughness::MaterialConstants *sceneData = reinterpret_cast<GLTFMetallicRoughness::MaterialConstants *>(materialConstants.allocation->GetMappedData());
    sceneData->colorFac = glm::vec4(1);
    sceneData->metallicRoughnessFac = glm::vec4(1, .5, 0, 0);

    _mainDeletionQueue.push([=, this]() {
        destroyBuffer(materialConstants);
        });

    materialResources.dataBuffer = materialConstants.buffer;
    materialResources.dataBufferOffset = 0;

    defaultData = metallicRoughnessMaterial.writeMaterial(_device, TsukiMaterialPass::TSUKI_MATERIAL_OPAQUE, materialResources, globalDescriptorAllocator);
}
//End Chapter 3

//Chapter 4
AllocatedImage TsukiEngine::createImage(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, bool mipmaps /*= false*/ ) {
    AllocatedImage newImage;
    newImage.imageFormat = format;
    newImage.imageExtent = extent;

    VkImageCreateInfo imageInfo = tsukiinit::tImageCreateInfo(format, usage, extent);
    if (mipmaps) {
        imageInfo.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(extent.width, extent.height)))) + 1;
    }

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(_allocator, &imageInfo, &allocInfo, &newImage.image, &newImage.allocation, nullptr));

    VkImageAspectFlags aspectFlag = (format == VK_FORMAT_D32_SFLOAT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageViewCreateInfo imageViewInfo = tsukiinit::tImageViewCreateInfo(format, newImage.image, aspectFlag);
    imageViewInfo.subresourceRange.levelCount = imageInfo.mipLevels;

    VK_CHECK(vkCreateImageView(_device, &imageViewInfo, nullptr, &newImage.imageView));
    return newImage;
}

AllocatedImage TsukiEngine::createImage(void *data, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, bool mipmaps /*= false*/ ) {
    size_t dataSize = extent.width * extent.height * extent.depth * 4; //We're assuming 8-bit RGBA channels

    AllocatedBuffer stagingBuffer = createBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    memcpy(stagingBuffer.info.pMappedData, data, dataSize);


    AllocatedImage newImage = createImage(extent, format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmaps);
    immediateSubmit([&](VkCommandBuffer commandBuffer) {
        tsukiutil::transitionImageLayout(commandBuffer, newImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;

        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = extent;

        //Copy the buffer to the image
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        //Transition layout for use in the fragment shader
        tsukiutil::transitionImageLayout(commandBuffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });

    destroyBuffer(stagingBuffer);

    return newImage;
}

void TsukiEngine::destroyImage(const AllocatedImage &image) {
    vkDestroyImageView(_device, image.imageView, nullptr);
    vmaDestroyImage(_allocator, image.image, image.allocation);
}
//End Chapter 4

AllocatedBuffer TsukiEngine::createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = nullptr;
    bufferInfo.size = allocSize;
    bufferInfo.usage = usage;
    
    VmaAllocationCreateInfo vmaAllocInfo{};
    vmaAllocInfo.usage = memoryUsage; //The usage flags influences where VMA places our buffer
    vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT; //Create memory mapped to the CPU's address space (like vkMapMemory)
    //The mapping isn't automatically kept consistent, for performance reasons
    AllocatedBuffer newBuffer;

    VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));

    return newBuffer;
}

void TsukiEngine::destroyBuffer(const AllocatedBuffer &buffer) {
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
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
    VK_CHECK(glfwCreateWindowSurface(_instance, _window, nullptr, &_surface));
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
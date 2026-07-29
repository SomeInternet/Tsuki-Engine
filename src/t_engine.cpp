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
#include "t_interop.h"
#include "t_pathtrace.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

//Windows includes
#include <VersionHelpers.h>
#include <aclapi.h>
#include <dxgi1_2.h>

#include "stb_image.h"

#include <nfd.hpp>

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

//WINDOWS SECURITY ATTRIBUTES
//===================================================================================================================
//TODO: Understand what these do
WindowsSecurityAttributes::WindowsSecurityAttributes()
{
    m_winPSecurityDescriptor = (PSECURITY_DESCRIPTOR)calloc(1, SECURITY_DESCRIPTOR_MIN_LENGTH + 2 * sizeof(void **));
    if (!m_winPSecurityDescriptor) {
        throw std::runtime_error("Failed to allocate memory for security descriptor");
    }

    PSID *ppSID = (PSID *)((PBYTE)m_winPSecurityDescriptor + SECURITY_DESCRIPTOR_MIN_LENGTH);
    PACL *ppACL = (PACL *)((PBYTE)ppSID + sizeof(PSID *));

    InitializeSecurityDescriptor(m_winPSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION);

    SID_IDENTIFIER_AUTHORITY sidIdentifierAuthority = SECURITY_WORLD_SID_AUTHORITY;
    AllocateAndInitializeSid(&sidIdentifierAuthority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, ppSID);

    EXPLICIT_ACCESS explicitAccess;
    ZeroMemory(&explicitAccess, sizeof(EXPLICIT_ACCESS));
    explicitAccess.grfAccessPermissions = STANDARD_RIGHTS_ALL | SPECIFIC_RIGHTS_ALL;
    explicitAccess.grfAccessMode = SET_ACCESS;
    explicitAccess.grfInheritance = INHERIT_ONLY;
    explicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    explicitAccess.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    explicitAccess.Trustee.ptstrName = (LPTSTR)*ppSID;

    SetEntriesInAcl(1, &explicitAccess, NULL, ppACL);

    SetSecurityDescriptorDacl(m_winPSecurityDescriptor, TRUE, *ppACL, FALSE);

    m_winSecurityAttributes.nLength = sizeof(m_winSecurityAttributes);
    m_winSecurityAttributes.lpSecurityDescriptor = m_winPSecurityDescriptor;
    m_winSecurityAttributes.bInheritHandle = TRUE;
}

SECURITY_ATTRIBUTES *WindowsSecurityAttributes::operator&() { return &m_winSecurityAttributes; }

WindowsSecurityAttributes::~WindowsSecurityAttributes()
{
    PSID *ppSID = (PSID *)((PBYTE)m_winPSecurityDescriptor + SECURITY_DESCRIPTOR_MIN_LENGTH);
    PACL *ppACL = (PACL *)((PBYTE)ppSID + sizeof(PSID *));

    if (*ppSID) {
        FreeSid(*ppSID);
    }
    if (*ppACL) {
        LocalFree(*ppACL);
    }
    free(m_winPSecurityDescriptor);
}

//MATERIALS
//===================================================================================================================
void TsukiMaterialPassPipelines::buildPipelines(TsukiEngine *engine) {
    VkShaderModule meshShader;
    if (!tsukiutil::loadShaderModule("./shaders/meshshader.spv", engine->_device, &meshShader)) {
        std::cerr << "Error building mesh shader" << std::endl;
    }

    VkPushConstantRange matrixRange{};
    matrixRange.offset = 0;
    matrixRange.size = sizeof(DrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayout layouts[] = {
        engine->_sceneDataDescriptorLayout,
        engine->_bindlessTextureDescriptorLayout,
        engine->_perMeshDescriptorLayout
    };

    VkPipelineLayoutCreateInfo meshPipelineLayoutInfo = tsukiinit::tPipelineLayoutCreateInfo();
    meshPipelineLayoutInfo.setLayoutCount = 3;
    meshPipelineLayoutInfo.pSetLayouts = layouts;
    meshPipelineLayoutInfo.pushConstantRangeCount = 1;
    meshPipelineLayoutInfo.pPushConstantRanges = &matrixRange;
    
    VkPipelineLayout newPipelineLayout;
    VK_CHECK(vkCreatePipelineLayout(engine->_device, &meshPipelineLayoutInfo, nullptr, &newPipelineLayout));

    opaquePipeline.layout = newPipelineLayout;
    transparentPipeline.layout = newPipelineLayout;

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = newPipelineLayout;
    pipelineBuilder.setShaders(meshShader, meshShader, "vertMain", "fragMain");
    pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.setMultisamplingNone();
    pipelineBuilder.disableBlending();
    pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_LESS_OR_EQUAL);

    pipelineBuilder.setColorAttachmentFormat(engine->_drawImage.imageFormat);
    pipelineBuilder.setDepthFormat(engine->_depthImage.imageFormat);

    opaquePipeline.pipeline = pipelineBuilder.build(engine->_device);

    pipelineBuilder.enableBlendingAdditive();
    pipelineBuilder.enableDepthTest(false, VK_COMPARE_OP_LESS_OR_EQUAL);
    transparentPipeline.pipeline = pipelineBuilder.build(engine->_device);

    vkDestroyShaderModule(engine->_device, meshShader, nullptr);
}
void TsukiMaterialPassPipelines::clearResources(VkDevice device) {
    vkDestroyPipelineLayout(device, opaquePipeline.layout, nullptr);

    vkDestroyPipeline(device, opaquePipeline.pipeline, nullptr);
    vkDestroyPipeline(device, transparentPipeline.pipeline, nullptr);
}

//SCENEGRAPH
//===================================================================================================================
void TMeshNode::queueDraw(const glm::mat4 &matrix, TsukiDrawContext &context) {
    glm::mat4 nodeMatrix = matrix * localTransform;

    TsukiVulkanRender vulkanRender;
    vulkanRender.indexBuffer = mesh->meshBuffers.indexBuffer.buffer;
    vulkanRender.meshDescriptorSet = mesh->meshBuffers.perMeshDescriptorSet;
    vulkanRender.indexCount = mesh->indexCount;
    vulkanRender.indexOffset = 0;
    vulkanRender.transform = nodeMatrix;
    vulkanRender.material = &context.engine->defaultData;

    //TODO: implement transparent objects for the viewport?
    context.opaqueObjects.push_back(vulkanRender);
    
    //TODO: Push instance to the engine IF the mesh hasn't been loaded
    if (!loadedToTlas && context.engine->_renderMode == TsukiRenderMode::TSUKI_RENDER_MODE_PATHTRACED_CUDA) {
        loadedToTlas = true;
        BLASInstance instance{};
        instance.blasNodeId = blasId;
        instance.transform = matrix * localTransform;
        context.engine->_sceneInstances.push_back(instance);

        //TODO: Push these transformations to the CUDA buffer
        context.engine->_tlasTransforms.push_back(nodeMatrix);
        context.engine->_tlasInvTransforms.push_back(glm::inverse(nodeMatrix)); //TODO: Optimize
    }
    TNode::queueDraw(nodeMatrix, context);
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

    int width, height, numChannels;
    unsigned char *data = stbi_load("assets/logo.png", &width, &height, &numChannels, 4); //Read in the image data to a buffer
    GLFWimage logo{};
    logo.width = width;
    logo.height = height;
    logo.pixels = data;
    glfwSetWindowIcon(_window, 1, &logo);
    stbi_image_free(data);

    glfwSetWindowUserPointer(_window, this); //Give the GLFW window a pointer to this instance of the engine

    glfwSetWindowIconifyCallback(_window, [](GLFWwindow *window, int iconified) {
        auto tsukiEngine = reinterpret_cast<TsukiEngine *>(glfwGetWindowUserPointer(window));
        tsukiEngine->_render = !iconified;
        });
    
    glfwSetKeyCallback(_window, TsukiInput::keyCallback);
    glfwSetMouseButtonCallback(_window, TsukiInput::mouseButtonCallback);
    glfwSetCursorPosCallback(_window, TsukiInput::cursorPosCallback);
    glfwSetScrollCallback(_window, TsukiInput::scrollCallback);

	//Initialize Vulkan
	initVulkan();
	initSwapChain();
	initCommands();
	initSyncStructures();

    initDescriptors();

    initPipelines();
    initMeshPipeline();

    initDefaultMeshData();

    initCudaData();

    initImgui();

    _mainDrawContext.engine = this;

    //TODO: Move elsewhere...?
    _materialDataBuffer = createExternalBuffer(1024 * sizeof(TsukiMaterialData),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    _mainDeletionQueue.push([&, this] {
        destroyBuffer(_materialDataBuffer);
        });
    
    //Load the Cornell Box scene
    std::string cornellBoxPath = "./models/cornellbox_plasticball.glb";
    auto cornellBoxFile = tsukiutil::loadGltf(this, cornellBoxPath);
    assert(cornellBoxFile.has_value());
    loadedScenes["Cornell Box"] = *cornellBoxFile;

    uploadCudaSceneData();

	_isInit = true;
}

void TsukiEngine::cleanup() {
	if (_isInit) {
        vkDeviceWaitIdle(_device);

        freeCudaSceneData();

        loadedScenes.clear();

        for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
            vkDestroyCommandPool(_device, _frames[i]._commandPool, nullptr);

            vkDestroyFence(_device, _frames[i]._renderFence, nullptr);
            vkDestroySemaphore(_device, _frames[i]._swapChainSemaphore, nullptr);

            _frames[i]._deletionQueue.flush();
        }

        materialPipelines.clearResources(_device);

        destroyBuffer(cudaImageBuffer);

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
    updateScene();

    VK_CHECK(vkWaitForFences(_device, 1, &getCurrFrame()._renderFence, true, 1000000000)); //Wait for 1 fence (the fence of the current frame) for up to 1 second

    //Clear frame-specific data
    //Clearing descriptor pools is very fast
    getCurrFrame()._deletionQueue.flush();
    getCurrFrame()._frameDescriptors.clearPools(_device);

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

    //BEGIN COMAND BUFFER
    VkCommandBuffer commandBuffer = getCurrFrame()._mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));
    VkCommandBufferBeginInfo commandBufferBeginInfo = tsukiinit::tCommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); //1-time command buffer (we re-record each frame)
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

    //Copy CUDA compute image into draw image
    if (_renderMode == TsukiRenderMode::TSUKI_RENDER_MODE_PATHTRACED_CUDA) {
        tsukiutil::transitionImageLayout(commandBuffer, _drawImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        tsukiutil::copyBufferToImage(commandBuffer, _drawImage.image, cudaImageBuffer.buffer, { _drawImage.imageExtent.width, _drawImage.imageExtent.height }, 0);

        tsukicudapathtrace::testPathtrace(&cudaData, _drawImage.imageExtent.width, _drawImage.imageExtent.height);
    }
    {
        VkImageLayout prevLayout = (_renderMode == TsukiRenderMode::TSUKI_RENDER_MODE_PATHTRACED_CUDA) ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
        tsukiutil::transitionImageLayout(commandBuffer, _drawImage.image, prevLayout, VK_IMAGE_LAYOUT_GENERAL);
    }

    //Transition the depth image to a writable format
    tsukiutil::transitionImageLayout(commandBuffer, _depthImage.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    //Draw geometry
    if (_renderMode == TsukiRenderMode::TSUKI_RENDER_MODE_VIEWPORT_FORWARD) {
        drawBackground(commandBuffer, _drawImage.image); //Clear color doesn't allow for color attachment optimal format
        tsukiutil::transitionImageLayout(commandBuffer, _drawImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        drawGeometry(commandBuffer, _swapChainImageViews[swapChainImageIndex]);
    }
    else if (_renderMode == TsukiRenderMode::TSUKI_RENDER_MODE_PATHTRACED_CUDA) {
        drawPathtraced(commandBuffer);
    }

    //Transition the draw image and the swapchain image to execute copy
    //Recall that within these helpers, we set a barrier that prevents further execution until the instructions before (compute pass) is finished
    tsukiutil::transitionImageLayout(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkImageLayout prevLayout = (_renderMode == TsukiRenderMode::TSUKI_RENDER_MODE_PATHTRACED_CUDA) ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    tsukiutil::transitionImageLayout(commandBuffer, _drawImage.image, prevLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    tsukiutil::copyImage(commandBuffer, _swapChainImages[swapChainImageIndex], _drawImage.image, _swapChainExtent, _drawExtent);

    //Transition swapchain image so IMGUI can draw to it
    tsukiutil::transitionImageLayout(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

    drawImgui(commandBuffer, _swapChainImageViews[swapChainImageIndex]);

    //Transition swapchain image to presentable format
    tsukiutil::transitionImageLayout(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    //End command buffer
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    //Submit the command buffer to the queue
    //TODO: Investigate this synchronization (I'm not sure I'm having the semaphores attached to the right pipeline stages...
    //If deadlock occurs, just presignal a semaphore
    VkCommandBufferSubmitInfo commandBufferSubmitInfo = tsukiinit::tCommandBufferSubmitInfo(commandBuffer);
    VkSemaphoreSubmitInfo waitCudaSemaphoreSubmitInfo = tsukiinit::tSemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
        cudaRenderDoneSemaphore);
    VkSemaphoreSubmitInfo waitSemaphoreSubmitInfo = tsukiinit::tSemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,
        getCurrFrame()._swapChainSemaphore); //Wait on the swap chain semaphore to begin writing to the image (when we receive the image, it's safe to write to)
    std::vector<VkSemaphoreSubmitInfo> waitSemaphoreSubmitInfos = {
        waitSemaphoreSubmitInfo,
        waitCudaSemaphoreSubmitInfo
    };

    //Define the submit info for the CUDA signal semaphore
    //TODO: Optimize
    VkSemaphoreSubmitInfo signalCudaSemaphoreSubmitInfo = tsukiinit::tSemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
        cudaWriteToBufferSemaphore);
    VkSemaphoreSubmitInfo signalSemaphoreSubmitInfo = tsukiinit::tSemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, 
        _renderSemaphores[swapChainImageIndex]); //Signal that rendering is complete afterwards (we can now present)
    std::vector<VkSemaphoreSubmitInfo> signalSemaphoreSubmitInfos = {
        signalSemaphoreSubmitInfo,
        signalCudaSemaphoreSubmitInfo
    };

    //TODO: Change submitInfos
    int numWaitSemaphores = (_renderMode == TsukiRenderMode::TSUKI_RENDER_MODE_PATHTRACED_CUDA) ? waitSemaphoreSubmitInfos.size() : 1;
    int numSignalSemaphores = (_renderMode == TsukiRenderMode::TSUKI_RENDER_MODE_PATHTRACED_CUDA) ? signalSemaphoreSubmitInfos.size() : 1;

    VkSubmitInfo2 submit = tsukiinit::tSubmitInfo(&commandBufferSubmitInfo, signalSemaphoreSubmitInfos.data(), waitSemaphoreSubmitInfos.data(), 
        numSignalSemaphores, numWaitSemaphores);
    
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
        //Begin clock
        auto start = std::chrono::system_clock::now();

		glfwPollEvents();

        //TODO: Make sure events process correctly
        if (_render) {

            //Recreate the swapchain if needed
            if (_resize) { resizeSwapChain(); }

            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            if (ImGui::Begin("Viewport")) {
                ImGui::SliderFloat("Render Scale",&renderScale, 0.3f, 1.f);
                {
                    std::vector<const char *> items = { "Solid", "Color", "Normal" };
                    static const char *selected = items[1];

                    if (ImGui::BeginCombo("Render Mode", selected)) {
                        for (int i = 0; i < items.size(); ++i) {
                            bool isSelected = (selected == items[i]);
                            if (ImGui::Selectable(items[i], isSelected)) {
                                selected = items[i];
                            }

                            isSelected = (selected == items[i]);

                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                                viewMode = static_cast<ViewMode>(i);
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                {
                    std::vector<const char *> items = { "Reinhard", "ACES", "AgX"};
                    static const char *selected = items[0];

                    if (ImGui::BeginCombo("Tone Mapping Mode", selected)) {
                        for (int i = 0; i < items.size(); ++i) {
                            bool isSelected = (selected == items[i]);
                            if (ImGui::Selectable(items[i], isSelected)) {
                                selected = items[i];
                            }

                            isSelected = (selected == items[i]);

                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                                _toneMapMode = static_cast<ToneMapMode>(i);
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                {
                    std::vector<const char *> items = { "None", "Acceleration Structures" };
                    static const char *selected = items[0];

                    if (ImGui::BeginCombo("Debug View", selected)) {
                        for (int i = 0; i < items.size(); ++i) {
                            bool isSelected = (selected == items[i]);
                            if (ImGui::Selectable(items[i], isSelected)) {
                                selected = items[i];
                            }

                            isSelected = (selected == items[i]);

                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                                if (cudaData.debugMode != static_cast<TsukiPathtraceDebug>(i)) { //Reset the accumulated image
                                    cudaData.numSamples = 0;
                                }
                                cudaData.debugMode = static_cast<TsukiPathtraceDebug>(i);
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                ImGui::End();
            }

            if (ImGui::Begin("Renderer")) {
                std::vector<const char*> items = { "Rasterized (Vulkan)", "Pathtraced(CUDA)" };
                static const char *selected = items[0];

                if (ImGui::BeginCombo("Render Mode", selected)) {
                    for (int i = 0; i < items.size(); ++i) {
                        bool isSelected = (selected == items[i]);
                        if (ImGui::Selectable(items[i], isSelected)) {
                            selected = items[i];
                        }

                        isSelected = (selected == items[i]);

                        if (isSelected) {
                            ImGui::SetItemDefaultFocus();
                            _renderMode = static_cast<TsukiRenderMode>(i);
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::End();
            }

            if (ImGui::Begin("Benchmarks")) {
                ImGui::Text("frametime %f ms", _benchmarker.frameTime);
                ImGui::Text("draw time %f ms", _benchmarker.meshDrawTime);
                ImGui::Text("update time %f ms", _benchmarker.sceneUpdateTime);
                ImGui::Text("triangles %i", _benchmarker.numTriangles);
                ImGui::Text("draws %i", _benchmarker.numDrawCalls);
                ImGui::End();
            }

            ImGui::Render();

            draw();
        }

        //Check time to determine total frame time
        auto end = std::chrono::system_clock::now();

        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        _benchmarker.frameTime = elapsed.count() / 1000.f;
    }  
}

//PUBLIC HELPERS
//===================================================================================================================
void TsukiEngine::drawBackground(VkCommandBuffer commandBuffer, VkImage image) {
    //Clear the im#if CLEAR_VALUE
    VkClearColorValue clearValue;
    clearValue = { { .1f, .1f, .1f, 1.0f } };
    VkImageSubresourceRange clearRange = tsukiinit::tImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
    vkCmdClearColorImage(commandBuffer, image, VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);
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

//Chapter 4
AllocatedImage TsukiEngine::createImage(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, bool mipmaps /*= false*/) {
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

AllocatedImage TsukiEngine::createImage(void *data, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, bool mipmaps /*= false*/) {
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

void TsukiEngine::updateScene() {
    //Setup clock for benchmarking
    auto start = std::chrono::system_clock::now();

    _mainDrawContext.opaqueObjects.clear();

    loadedScenes["Cornell Box"]->queueDraw(glm::mat4(1), _mainDrawContext);

    //Update the scene data struct, to be passed as a uniform buffer later
    {
        sceneData.view = camera.getView();
        sceneData.proj = glm::perspective(glm::radians(camera.fov), static_cast<float>(_windowExtent.width) / static_cast<float>(_windowExtent.height),
            .1f, 10000.f);

        //Invert y for Vulkan
        sceneData.proj[1][1] *= -1;
        sceneData.viewproj = sceneData.proj * sceneData.view;
        sceneData.camPos = glm::vec3(glm::inverse(sceneData.view) * glm::vec4(0, 0, 0, 1));

        sceneData.ambient = .1f;

        //Update CUDA's copy of the camera (only once the device buffer has been allocated)
        if (cudaData.d_camera) {
            TsukiCudaCamera cudaCamera = camera.toCudaCamera();
            CUDA_CHECK(cudaMemcpy(cudaData.d_camera, &cudaCamera, sizeof(TsukiCudaCamera), cudaMemcpyHostToDevice));
        }
    }

    if (_renderMode == TsukiRenderMode::TSUKI_RENDER_MODE_PATHTRACED_CUDA && _tlasDirty) {
        _tlasDirty = false;
        std::vector<BVHNode> blas;
        for (auto &bvh : _blas) {
            blas.push_back(bvh[0]);
        }

        _tlas = tsukibvh::buildTLAS(blas, _sceneInstances, _tlasTransforms);

        //Re-upload TLAS to CUDA
        if (hostAS.d_tlas) {
            CUDA_CHECK(cudaFree(hostAS.d_tlas));
            hostAS.d_tlas = nullptr;
        }
        hostAS.tlasSize = static_cast<int>(_tlas.size());
        if (!_tlas.empty()) {
            CUDA_CHECK(cudaMalloc(&hostAS.d_tlas, _tlas.size() * sizeof(TLASNode)));
            CUDA_CHECK(cudaMemcpy(hostAS.d_tlas, _tlas.data(), _tlas.size() * sizeof(TLASNode), cudaMemcpyHostToDevice));
        }

        //Upload the transforms and inverse transforms to CUDA
        if (!_tlasTransforms.empty()) {
            CUDA_CHECK(cudaMalloc(&hostAS.d_transforms, _tlasTransforms.size() * sizeof(glm::mat4)));
            CUDA_CHECK(cudaMemcpy(hostAS.d_transforms, _tlasTransforms.data(), _tlasTransforms.size() * sizeof(glm::mat4), cudaMemcpyHostToDevice));

            CUDA_CHECK(cudaMalloc(&hostAS.d_invTransforms, _tlasInvTransforms.size() * sizeof(glm::mat4)));
            CUDA_CHECK(cudaMemcpy(hostAS.d_invTransforms, _tlasInvTransforms.data(), _tlasInvTransforms.size() * sizeof(glm::mat4), cudaMemcpyHostToDevice));
        }

        if (!cudaData.d_accelerationStructures) {
            CUDA_CHECK(cudaMalloc(&cudaData.d_accelerationStructures, sizeof(TsukiCudaAccelerationStructures)));
        }
        CUDA_CHECK(cudaMemcpy(cudaData.d_accelerationStructures, &hostAS, sizeof(TsukiCudaAccelerationStructures), cudaMemcpyHostToDevice));
    }

    auto end = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    _benchmarker.sceneUpdateTime = elapsed.count() / 1000.f;
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

AllocatedBuffer TsukiEngine::createExternalBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) {
    //Gotta mind the offsets because VMA suballocates many buffers within a single VkDeviceMemory at different offsets
    VkExternalMemoryBufferCreateInfo externalBufferInfo{};
    externalBufferInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    externalBufferInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.pNext = &externalBufferInfo;
    bufferInfo.size = allocSize;
    bufferInfo.usage = usage;

    WindowsSecurityAttributes windowsSecurityAttributes;

    VkExportMemoryWin32HandleInfoKHR vulkanExportMemoryHandleInfo{};
    vulkanExportMemoryHandleInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
    vulkanExportMemoryHandleInfo.pNext = nullptr;
    vulkanExportMemoryHandleInfo.pAttributes = &windowsSecurityAttributes;
    vulkanExportMemoryHandleInfo.dwAccess = DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE;
    vulkanExportMemoryHandleInfo.name = (LPCWSTR)NULL;

    VkExportMemoryAllocateInfo exportMemoryAllocInfo{};
    exportMemoryAllocInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    exportMemoryAllocInfo.pNext = &vulkanExportMemoryHandleInfo;
    exportMemoryAllocInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VmaAllocationCreateInfo vmaAllocInfo{};
    vmaAllocInfo.usage = memoryUsage; //The usage flags influences where VMA places our buffer
    vmaAllocInfo.pool = _vmaExternalPool;

    AllocatedBuffer newBuffer;

    VK_CHECK(vmaCreateBuffer(_allocator, &bufferInfo, &vmaAllocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));

    return newBuffer;
}

AllocatedImage TsukiEngine::createExternalImage(void *data, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, bool mipmaps) {
    size_t dataSize = extent.width * extent.height * extent.depth * 4;

    AllocatedBuffer stagingBuffer = createBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    memcpy(stagingBuffer.info.pMappedData, data, dataSize);

    VkExternalMemoryImageCreateInfo externalImageInfo{};
    externalImageInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    externalImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkImageCreateInfo imageInfo = tsukiinit::tImageCreateInfo(format, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, extent);
    imageInfo.pNext = &externalImageInfo;
    if (mipmaps) {
        imageInfo.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(extent.width, extent.height)))) + 1;
    }

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    allocInfo.pool = _vmaExternalPool;

    AllocatedImage newImage;
    newImage.imageFormat = format;
    newImage.imageExtent = extent;

    VK_CHECK(vmaCreateImage(_allocator, &imageInfo, &allocInfo, &newImage.image, &newImage.allocation, nullptr));

    VkImageAspectFlags aspectFlag = (format == VK_FORMAT_D32_SFLOAT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageViewCreateInfo imageViewInfo = tsukiinit::tImageViewCreateInfo(format, newImage.image, aspectFlag);
    imageViewInfo.subresourceRange.levelCount = imageInfo.mipLevels;

    VK_CHECK(vkCreateImageView(_device, &imageViewInfo, nullptr, &newImage.imageView));

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

        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.buffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        tsukiutil::transitionImageLayout(commandBuffer, newImage.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        });

    destroyBuffer(stagingBuffer);

    return newImage;
}

void TsukiEngine::destroyBuffer(const AllocatedBuffer &buffer) {
    vmaDestroyBuffer(_allocator, buffer.buffer, buffer.allocation);
}

//MESH HELPERS
//===================================================================================================================
DeviceMeshBuffers TsukiEngine::uploadMesh(std::span<uint32_t> indices, std::span<glm::vec4> positions, std::span<glm::vec4> normals, std::span<glm::vec4> colors, std::span<glm::vec2> uvs, std::span<uint32_t> materialLookup, int startMaterialIndex) {
    const size_t posBufferSize = positions.size() * sizeof(glm::vec4);
    const size_t normalBufferSize = normals.size() * sizeof(glm::vec4);
    const size_t colorBufferSize = colors.size() * sizeof(glm::vec4);
    const size_t uvBufferSize = uvs.size() * sizeof(glm::vec2);
    const size_t indexBufferSize = indices.size() * sizeof(uint32_t);
    const size_t materialLookupSize = materialLookup.size() * sizeof(uint32_t);

    DeviceMeshBuffers mesh;

    //Create external buffers for the mesh attributes
    mesh.posBuffer = createExternalBuffer(posBufferSize,

        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    mesh.normalBuffer = createExternalBuffer(normalBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    mesh.colorBuffer = createExternalBuffer(colorBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    mesh.uvBuffer = createExternalBuffer(uvBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    mesh.indexBuffer = createExternalBuffer(indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    mesh.materialLookupBuffer = createExternalBuffer(materialLookupSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);
    //The TsukiGLTF destructor will destroy these buffers

    //Giant staging buffer for all data
    {
        size_t totalSize = posBufferSize + normalBufferSize + colorBufferSize + uvBufferSize + indexBufferSize + materialLookupSize;
        AllocatedBuffer stagingBuffer = createBuffer(totalSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        void *data = stagingBuffer.allocation->GetMappedData();

        size_t offset = 0;
        memcpy((char *)data + offset, positions.data(), posBufferSize); offset += posBufferSize;
        memcpy((char *)data + offset, normals.data(), normalBufferSize); offset += normalBufferSize;
        memcpy((char *)data + offset, colors.data(), colorBufferSize); offset += colorBufferSize;
        memcpy((char *)data + offset, uvs.data(), uvBufferSize); offset += uvBufferSize;
        memcpy((char *)data + offset, indices.data(), indexBufferSize); offset += indexBufferSize;
        memcpy((char *)data + offset, materialLookup.data(), materialLookupSize);

        //Copy the data over to the device-local buffers
        immediateSubmit([&](VkCommandBuffer commandBuffer) {
            offset = 0;
            VkBufferCopy copy{};
            copy.dstOffset = 0; copy.srcOffset = offset; copy.size = posBufferSize;
            vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, mesh.posBuffer.buffer, 1, &copy); offset += posBufferSize;

            copy.dstOffset = 0; copy.srcOffset = offset; copy.size = normalBufferSize;
            vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, mesh.normalBuffer.buffer, 1, &copy); offset += normalBufferSize;

            copy.dstOffset = 0; copy.srcOffset = offset; copy.size = colorBufferSize;
            vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, mesh.colorBuffer.buffer, 1, &copy); offset += colorBufferSize;

            copy.dstOffset = 0; copy.srcOffset = offset; copy.size = uvBufferSize;
            vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, mesh.uvBuffer.buffer, 1, &copy); offset += uvBufferSize;

            copy.dstOffset = 0; copy.srcOffset = offset; copy.size = indexBufferSize;
            vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, mesh.indexBuffer.buffer, 1, &copy); offset += indexBufferSize;

            copy.dstOffset = 0; copy.srcOffset = offset; copy.size = materialLookupSize;
            vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, mesh.materialLookupBuffer.buffer, 1, &copy);
            });

        destroyBuffer(stagingBuffer);
    }

    //Upload the material data
    int numMaterials = materials.size() - startMaterialIndex;
    if (numMaterials > 0) {
        AllocatedBuffer stagingBuffer = createBuffer(numMaterials * sizeof(TsukiMaterialData),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        void *data = stagingBuffer.allocation->GetMappedData();
        memcpy(data, reinterpret_cast<TsukiMaterialData*>(&(materials[startMaterialIndex])), numMaterials * sizeof(TsukiMaterialData));

        //Copy the data over to the device-local buffers
        immediateSubmit([&](VkCommandBuffer commandBuffer) {
            VkBufferCopy materialCopy{};
            materialCopy.dstOffset = startMaterialIndex * sizeof(TsukiMaterialData);
            materialCopy.srcOffset = 0;
            materialCopy.size = numMaterials * sizeof(TsukiMaterialData);
            vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, _materialDataBuffer.buffer, 1, &materialCopy);
            });

        destroyBuffer(stagingBuffer);
    }

    mesh.perMeshDescriptorSet = _meshDescriptorAllocator.allocate(_device, _perMeshDescriptorLayout);

    DescriptorWriter writer;
    writer.writeBuffer(0, mesh.posBuffer.buffer, posBufferSize, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.writeBuffer(1, mesh.normalBuffer.buffer, normalBufferSize, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.writeBuffer(2, mesh.colorBuffer.buffer, colorBufferSize, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.writeBuffer(3, mesh.uvBuffer.buffer, uvBufferSize, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.writeBuffer(4, mesh.materialLookupBuffer.buffer, materialLookupSize, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writer.updateSet(_device, mesh.perMeshDescriptorSet);

    //TODO: Update the descriptor sets for the images
    {
        DescriptorWriter writer;
        //Binding 0) material data array
        writer.writeBuffer(0, _materialDataBuffer.buffer, materials.size() * sizeof(TsukiMaterialData), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        //Binding 1) texture array
        std::vector<VkImageView> textureViews;
        textureViews.reserve(materialTextures.size());
        for (auto &tex : materialTextures) {
            textureViews.push_back(tex.imageView);
        }
        if (textureViews.empty()) {
            textureViews.push_back(_errorCheckerboardImage.imageView);
        }
        writer.writeImageArray(1, textureViews.data(), (uint32_t)textureViews.size(), VK_NULL_HANDLE,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);

        //Binding 2) sampler
        writer.writeImage(2, VK_NULL_HANDLE, _defaultSamplerLinear,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_SAMPLER);

        writer.updateSet(_device, _bindlessDescriptorSet);
    }

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
    _drawImage.imageFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
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

    //Initialize the descriptor allocators for each frame in flight
    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        std::vector<DynamicDescriptorAllocator::PoolSizeRatio> frameSizes = {
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
        };
    }

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

    //Initialize structs to export semaphores from Vulkan to CUDA
    VkSemaphoreTypeCreateInfo semaphoreTypeInfo{};
    semaphoreTypeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    semaphoreTypeInfo.semaphoreType = VK_SEMAPHORE_TYPE_BINARY;

    VkExportSemaphoreCreateInfo exportSemaphoreInfo{};
    exportSemaphoreInfo.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
    exportSemaphoreInfo.pNext = &semaphoreTypeInfo;
    exportSemaphoreInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    semaphoreCreateInfo.pNext = &exportSemaphoreInfo;

    VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &cudaWriteToBufferSemaphore));
    VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &cudaRenderDoneSemaphore));

    //Run interop functions to populate the CUDA external semaphores in cudaData
    tsukiutil::getCudaSemaphore(this, &(cudaData._cudaCopyFinishedSemaphore), &cudaWriteToBufferSemaphore);
    tsukiutil::getCudaSemaphore(this, &(cudaData._cudaSampleFinishedSemaphore), &cudaRenderDoneSemaphore);

    //Push deletion to the main deletion queue
    _mainDeletionQueue.push([=]() {
        vkDestroySemaphore(_device, cudaWriteToBufferSemaphore, nullptr);
        vkDestroySemaphore(_device, cudaRenderDoneSemaphore, nullptr);
        });

    //Initialize immediate submit
    VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_immFence));

    _mainDeletionQueue.push([=]() {vkDestroyFence(_device, _immFence, nullptr); });

    //Pre-signal cudaWriteToBufferSemaphore via queue submission so CUDA's
    //first wait doesn't deadlock (binary semaphores can only be signaled
    //by the queue, not via vkSignalSemaphore).
    {
        VkCommandBuffer signalCmd;
        VkCommandBufferAllocateInfo allocInfo = tsukiinit::tCommandBufferAllocateInfo(_immCommandPool);
        VK_CHECK(vkAllocateCommandBuffers(_device, &allocInfo, &signalCmd));

        VkCommandBufferBeginInfo beginInfo = tsukiinit::tCommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        VK_CHECK(vkBeginCommandBuffer(signalCmd, &beginInfo));
        VK_CHECK(vkEndCommandBuffer(signalCmd));

        VkCommandBufferSubmitInfo cmdBufInfo = tsukiinit::tCommandBufferSubmitInfo(signalCmd);
        VkSemaphoreSubmitInfo signalSemaphoreInfo = tsukiinit::tSemaphoreSubmitInfo(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, cudaWriteToBufferSemaphore);
        VkSubmitInfo2 submit = tsukiinit::tSubmitInfo(&cmdBufInfo, &signalSemaphoreInfo, nullptr, 1, 0);

        VK_CHECK(vkResetFences(_device, 1, &_immFence));
        VK_CHECK(vkQueueSubmit2(_graphicsQueue, 1, &submit, _immFence));
        VK_CHECK(vkWaitForFences(_device, 1, &_immFence, true, 9999999999));

        vkFreeCommandBuffers(_device, _immCommandPool, 1, &signalCmd);
    }
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
    std::vector<DynamicDescriptorAllocator::PoolSizeRatio> sizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
    };
    globalDescriptorAllocator.init(_device, 10, sizes);

    DescriptorLayoutBuilder builder;
    builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    _drawImageDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_COMPUTE_BIT);

    _drawImageDescriptors = globalDescriptorAllocator.allocate(_device, _drawImageDescriptorLayout);

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    imageInfo.imageView = _drawImage.imageView;

    VkWriteDescriptorSet drawImageWrite{};
    drawImageWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;;
    drawImageWrite.pNext = nullptr;
    drawImageWrite.dstBinding = 0;
    drawImageWrite.dstSet = _drawImageDescriptors;
    drawImageWrite.descriptorCount = 1;
    drawImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    drawImageWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(_device, 1, &drawImageWrite, 0, nullptr);

    //Set 0) Scene data uniform buffer
    {
        DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        _sceneDataDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    //Set 1) Bindless textures + material data + sampler
    {
        //Let Vulkan know that the sampled image array be bindless, as otherwise Vulkan believes it's a constant sized array
        
        std::vector<VkDescriptorBindingFlags> bindingFlags = {
            0, //Binding 0, the material data array
            VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT, //Binding 1, the textures
            0 //Binding 2, the sampler
        };

        VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
        bindingFlagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        bindingFlagsInfo.bindingCount = bindingFlags.size();
        bindingFlagsInfo.pBindingFlags = bindingFlags.data();

        DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); //Material data array
        builder.addBinding(1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_BINDLESS_TEXTURES); //Texture array
        builder.addBinding(2, VK_DESCRIPTOR_TYPE_SAMPLER); //Shared sampler
        _bindlessTextureDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, &bindingFlagsInfo, VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT);
    }

    //Set 2) Per-mesh buffers (pos, normal, color, uv, material lookup)
    {
        DescriptorLayoutBuilder builder;
        builder.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // pos buffer
        builder.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // normal buffer
        builder.addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // color buffer
        builder.addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // uv buffer
        builder.addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER); // material lookup buffer
        _perMeshDescriptorLayout = builder.build(_device, VK_SHADER_STAGE_VERTEX_BIT);
    }

    //Persistent allocator for per-mesh descriptor sets
    {
        std::vector<DynamicDescriptorAllocator::PoolSizeRatio> meshSizes = {
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5}
        };
        _meshDescriptorAllocator.init(_device, 100, meshSizes);
    }

    //Per-frame descriptor pools
    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        std::vector<DynamicDescriptorAllocator::PoolSizeRatio> framePoolSizes = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}
        };

        _frames[i]._frameDescriptors = DynamicDescriptorAllocator{};
        _frames[i]._frameDescriptors.init(_device, 2, framePoolSizes);

        _mainDeletionQueue.push([&, i]() {
            _frames[i]._frameDescriptors.destroyPools(_device);
            });
    }

    //Create the descriptor "pool" to allocate the bindless descriptor set from
    std::vector<DynamicDescriptorAllocator::PoolSizeRatio> bindlessPoolSizes = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MAX_BINDLESS_TEXTURES},
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1}
    };
    _bindlessDescriptorAllocator.init(_device, 1, bindlessPoolSizes, VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT);

    //Create the descriptor set for the bindless textures
    _bindlessDescriptorSet = _bindlessDescriptorAllocator.allocate(_device, _bindlessTextureDescriptorLayout);

    _mainDeletionQueue.push([&]() {
        globalDescriptorAllocator.destroyPools(_device);
        _meshDescriptorAllocator.destroyPools(_device);
        _bindlessDescriptorAllocator.destroyPools(_device);
        vkDestroyDescriptorSetLayout(_device, _drawImageDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _sceneDataDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _bindlessTextureDescriptorLayout, nullptr);
        vkDestroyDescriptorSetLayout(_device, _perMeshDescriptorLayout, nullptr);
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
    computeLayout.pSetLayouts = &_drawImageDescriptorLayout; //Plug in the descriptor set layout, promising certain kinds of resources

    VkPushConstantRange toneMapRange{};
    toneMapRange.offset = 0;
    toneMapRange.size = sizeof(ToneMapMode);
    toneMapRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    computeLayout.pushConstantRangeCount = 1;
    computeLayout.pPushConstantRanges = &toneMapRange;

    VK_CHECK(vkCreatePipelineLayout(_device, &computeLayout, nullptr, &_pathtracerPipelineLayout));

    VkShaderModule computeColorCorrectShader;
    if (!tsukiutil::loadShaderModule("./shaders/colorcorrect.spv", _device, &computeColorCorrectShader)) {
        std::cerr << "Error building color correction compute shader" << std::endl;
    }

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.pNext = nullptr;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = computeColorCorrectShader;
    stageInfo.pName = "main"; //Entry point (?)

    VkComputePipelineCreateInfo computePipelineCreateInfo{};
    computePipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.pNext = nullptr;
    computePipelineCreateInfo.layout = _pathtracerPipelineLayout;
    computePipelineCreateInfo.stage = stageInfo;

    stageInfo.module = computeColorCorrectShader;
    computePipelineCreateInfo.stage = stageInfo; //Was copied by value, so I have to reassign the modified version

    VK_CHECK(vkCreateComputePipelines(_device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &_pathtracerPipeline));

    vkDestroyShaderModule(_device, computeColorCorrectShader, nullptr);

    _mainDeletionQueue.push([=]() {
        vkDestroyPipelineLayout(_device, _pathtracerPipelineLayout, nullptr);
        vkDestroyPipeline(_device, _pathtracerPipeline, nullptr);
        });

    materialPipelines.buildPipelines(this);
}

void TsukiEngine::initCudaData() {
    //TODO: Create a dedicated VMA pool for external CUDA buffers
    //The export info needs to remain alive for the duration of the pool, so I'm promoting this to member of TsukiEngine. Yippee
    exportInfo = {};
    exportInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkBufferCreateInfo tempBufferInfo{};
    tempBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    tempBufferInfo.pNext = nullptr;
    tempBufferInfo.size = 1024; //Dummy value
    tempBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | 
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;

    VmaAllocationCreateInfo tempAllocInfo{};
    tempAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    uint32_t memoryTypeIndex{ 0 };
    vmaFindMemoryTypeIndexForBufferInfo(_allocator, &tempBufferInfo, &tempAllocInfo, &memoryTypeIndex);

    VmaPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.memoryTypeIndex = memoryTypeIndex;
    poolCreateInfo.pMemoryAllocateNext = &exportInfo;
    poolCreateInfo.maxBlockCount = 4; //TODO: Maybe find a way to set this dynamically?
    //Default block size is 256MB, so this allocates about 1GB of VRAM

    VK_CHECK(vmaCreatePool(_allocator, &poolCreateInfo, &_vmaExternalPool));
    _mainDeletionQueue.push([&] {
            vmaDestroyPool(_allocator, _vmaExternalPool);
        });

    //Allocate device buffer for CUDA draws

    int imageBufferSize = WIDTH * HEIGHT * sizeof(glm::vec4);
    std::cerr << "Creating CUDA image buffer, size: " << imageBufferSize << std::endl;
    cudaImageBuffer = createExternalBuffer(imageBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_GPU_ONLY);
    cudaData.imageBufferSize = imageBufferSize;

    //Register the cuda image buffer's memory with CUDA
    VmaAllocationInfo2 cudaImageBufferAllocInfo{};
    vmaGetAllocationInfo2(_allocator, cudaImageBuffer.allocation, &cudaImageBufferAllocInfo);


    VkBufferDeviceAddressInfo deviceAddressInfo{};
    deviceAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    deviceAddressInfo.buffer = cudaImageBuffer.buffer;

    //TODO: Remove excess code
    //cudaData.imageBufferAddress = vkGetBufferDeviceAddress(_device, &deviceAddressInfo);
    tsukiutil::getCudaExternalMemory(this, &(cudaData.imageBuffer), &(cudaData.imageBufferMemory),
        &(cudaImageBufferAllocInfo.allocationInfo.deviceMemory), cudaImageBufferAllocInfo.blockSize,
        cudaImageBufferAllocInfo.allocationInfo.offset, imageBufferSize);

    CUDA_CHECK(cudaMalloc(&cudaData.d_curandStates, WIDTH * HEIGHT * sizeof(curandState)));
    
    //Init the curands for sampling
    tsukicudapathtrace::initCurand(&cudaData, WIDTH, HEIGHT);

    _mainDeletionQueue.push([&]() {
        cudaFree(cudaData.d_curandStates);
        });

    //The CUDA vertex buffer and index buffer will be populated when a glTF is loaded
}

void TsukiEngine::uploadCudaSceneData() {
    if (meshes.empty()) return;

    //TODO: Implement reloading on uploading more meshes

    const int numMeshes = static_cast<int>(meshes.size());

    std::vector<TsukiCudaMesh> hostMeshes(numMeshes);

    //We also need to keep the cudaExternalMemory_t handles alive for the lifetime of the scene
    
    //Lambda to convert Vulkan VMA buffers to CUDA external memory
    auto importBuffer = [&](AllocatedBuffer &vkBuffer, size_t byteSize,
        cudaExternalMemory_t &outExtMemory,
        void **outDPtr)
        {
            VmaAllocationInfo2 allocInfo{};
            vmaGetAllocationInfo2(_allocator, vkBuffer.allocation, &allocInfo);

            tsukiutil::getCudaExternalMemory(this, outDPtr, &outExtMemory, &allocInfo.allocationInfo.deviceMemory,
                allocInfo.blockSize, allocInfo.allocationInfo.offset, byteSize);
        };

    for (int i = 0; i < numMeshes; ++i) {
        auto &mesh = *meshes[i];
        auto &buffers = mesh.meshBuffers;
        TsukiCudaMesh &cm = hostMeshes[i];

        //Indices
        const size_t indexBytes = mesh.indexCount * sizeof(uint32_t);
        cm.numIndices = static_cast<int>(mesh.indexCount);
        importBuffer(buffers.indexBuffer, indexBytes,
            cm.indexBufferMemory,
            reinterpret_cast<void **>(&cm.d_indexBuffer));

        //Positions
        //Vertex count is derived from the buffer size stored in the
        //VMA allocation info
        {
            VmaAllocationInfo posAI{};
            vmaGetAllocationInfo(_allocator, buffers.posBuffer.allocation, &posAI);
            const size_t posBytes = posAI.size;
            cm.numVertices = static_cast<int>(posBytes / sizeof(glm::vec4));
            importBuffer(buffers.posBuffer, posBytes,
                cm.posMemory,
                reinterpret_cast<void **>(&cm.d_pos));
        }

        //Normals
        {
            VmaAllocationInfo ai{};
            vmaGetAllocationInfo(_allocator, buffers.normalBuffer.allocation, &ai);
            importBuffer(buffers.normalBuffer, ai.size,
                cm.normalMemory,
                reinterpret_cast<void **>(&cm.d_normal));
        }

        //Vertex colors
        {
            VmaAllocationInfo ai{};
            vmaGetAllocationInfo(_allocator, buffers.colorBuffer.allocation, &ai);
            importBuffer(buffers.colorBuffer, ai.size,
                cm.colorMemory,
                reinterpret_cast<void **>(&cm.d_color));
        }

        //UVs
        {
            VmaAllocationInfo ai{};
            vmaGetAllocationInfo(_allocator, buffers.uvBuffer.allocation, &ai);
            importBuffer(buffers.uvBuffer, ai.size,
                cm.uvMemory,
                reinterpret_cast<void **>(&cm.d_uv));
        }

        //UPLOAD MATERIAL LOOKUP BUFFER
        {
            VmaAllocationInfo ai{};
            vmaGetAllocationInfo(_allocator, buffers.materialLookupBuffer.allocation, &ai);
            cm.materialLookupBufferSize = static_cast<int>(ai.size / sizeof(int));
            importBuffer(buffers.materialLookupBuffer, ai.size,
                cm.MaterialLookupBufferMemory,
                reinterpret_cast<void **>(&cm.d_materialLookupBuffer));
        }
    }

    //UPLOAD TSUKICUDAMESHES TO DEVICE
    {
        const size_t meshArrayBytes = numMeshes * sizeof(TsukiCudaMesh);

        // Free any previous allocation.
        if (cudaData.d_meshes) {
            CUDA_CHECK(cudaFree(cudaData.d_meshes));
            cudaData.d_meshes = nullptr;
        }

        CUDA_CHECK(cudaMalloc(&cudaData.d_meshes, meshArrayBytes));
        CUDA_CHECK(cudaMemcpy(cudaData.d_meshes,
            hostMeshes.data(),
            meshArrayBytes,
            cudaMemcpyHostToDevice));
        cudaData.numMeshes = numMeshes;
    }

    //UPLOAD BLAS'S INFORMATION TO THE DEVICE

    //Allocate / free the per-mesh acceleration structure container.

    const int numBVH = _blas.size();
    hostAS.numBVH = numBVH;

    //Host-side arrays of sizes and device pointers
    std::vector<int>       bvhSizesHost(numBVH);
    std::vector<BVHNode *> bvhPtrsHost(numBVH, nullptr);

    for (int i = 0; i < numBVH; ++i) {
        const auto &bvh = _blas[i];
        bvhSizesHost[i] = static_cast<int>(bvh.size());
        const size_t nodeBytes = bvh.size() * sizeof(BVHNode);

        CUDA_CHECK(cudaMalloc(&bvhPtrsHost[i], nodeBytes));
        CUDA_CHECK(cudaMemcpy(bvhPtrsHost[i],
            bvh.data(),
            nodeBytes,
            cudaMemcpyHostToDevice));
    }

    //Upload the sizes of all the BLAS's
    {
        const size_t sizeArrayBytes = numBVH * sizeof(int);
        CUDA_CHECK(cudaMalloc(&hostAS.d_bvhSizes, sizeArrayBytes));
        CUDA_CHECK(cudaMemcpy(hostAS.d_bvhSizes,
            bvhSizesHost.data(),
            sizeArrayBytes,
            cudaMemcpyHostToDevice));
    }

    //Upload the array of pointers to the nodes of the BLAS roots
    {
        const size_t ptrArrayBytes = numBVH * sizeof(BVHNode *);
        CUDA_CHECK(cudaMalloc(&hostAS.d_bvh, ptrArrayBytes));
        CUDA_CHECK(cudaMemcpy(hostAS.d_bvh,
            bvhPtrsHost.data(),
            ptrArrayBytes,
            cudaMemcpyHostToDevice));
    }

    //Keep a host-side copy of the per-mesh BVH device pointers so we can free
    //them later. hostAS.d_bvh lives in device memory, so it cannot be indexed
    //from the host during cleanup.
    blasPtrs = bvhPtrsHost;

    //Upload TLAS nodes
    if (!_tlas.empty()) {
        const size_t tlasBytes = _tlas.size() * sizeof(TLASNode);
        hostAS.tlasSize = static_cast<int>(_tlas.size());

        CUDA_CHECK(cudaMalloc(&hostAS.d_tlas, tlasBytes));
        CUDA_CHECK(cudaMemcpy(hostAS.d_tlas,
            _tlas.data(),
            tlasBytes,
            cudaMemcpyHostToDevice));
    }

    //UPLOAD TSUKICUDAACCELERATIONSTRUCTURES
    {
        if (cudaData.d_accelerationStructures) {
            CUDA_CHECK(cudaFree(cudaData.d_accelerationStructures));
            cudaData.d_accelerationStructures = nullptr;
        }

        CUDA_CHECK(cudaMalloc(&cudaData.d_accelerationStructures,
            sizeof(TsukiCudaAccelerationStructures)));
        CUDA_CHECK(cudaMemcpy(cudaData.d_accelerationStructures,
            &hostAS,
            sizeof(TsukiCudaAccelerationStructures),
            cudaMemcpyHostToDevice));
    }

    //Malloc and upload camera data
    if (cudaData.d_camera) {
        CUDA_CHECK(cudaFree(cudaData.d_camera));
        cudaData.d_camera = nullptr;
    }
    CUDA_CHECK(cudaMalloc(&cudaData.d_camera, sizeof(TsukiCudaCamera)));

    //Export the Vulkan material data buffer to CUDA
    VmaAllocationInfo materialsAllocInfo{};
    vmaGetAllocationInfo(_allocator, _materialDataBuffer.allocation, &materialsAllocInfo);
    importBuffer(_materialDataBuffer, materialsAllocInfo.size, cudaData.materialBufferMemory, reinterpret_cast<void**>(&cudaData.d_materialBuffer));

    //TODO: Malloc and upload the transformation data
}

void TsukiEngine::freeCudaSceneData() {
    //Only take charge for the stuff allocated BY CUDA
    //Use the host-side copy of the pointers: hostAS.d_bvh is itself a device
    //pointer, so hostAS.d_bvh[i] would dereference GPU memory on the host (-> access violation)
    for (BVHNode *blasPtr : blasPtrs) { //Free the device-side BVH's
        CUDA_CHECK(cudaFree(blasPtr));
    }
    blasPtrs.clear();

    //Free the device side TLAS, and helper arrays
    CUDA_CHECK(cudaFree(hostAS.d_bvh)); //The pointer array itself
    CUDA_CHECK(cudaFree(hostAS.d_bvhSizes));
    CUDA_CHECK(cudaFree(hostAS.d_tlas));

    //Free the trnasofrmations and inverse transformations
    CUDA_CHECK(cudaFree(hostAS.d_transforms));
    CUDA_CHECK(cudaFree(hostAS.d_invTransforms));

    //Free the device side acceleration structures struct
    CUDA_CHECK(cudaFree(cudaData.d_accelerationStructures));

    //Free the camera
    CUDA_CHECK(cudaFree(cudaData.d_camera));
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
    ImGui_ImplGlfw_InitForVulkan(_window, false);

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
    initImguiTheme();
    //ImGui_ImplVulkan_CreateFontsTexture();

    //Add to destroy queue
    _mainDeletionQueue.push([=]() {
        ImGui_ImplVulkan_Shutdown();
        vkDestroyDescriptorPool(_device, imguiPool, nullptr);
        });
}

void TsukiEngine::initImguiTheme() {
    //Catppuccin Mocha Palette (credit: https://github.com/ocornut/imgui/issues/707#issuecomment-3592676777, https://github.com/catppuccin/catppuccin)


    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    const ImVec4 base       = ImVec4(0.117f, 0.117f, 0.172f, 1.0f); // #1e1e2e
    const ImVec4 mantle     = ImVec4(0.109f, 0.109f, 0.156f, 1.0f); // #181825
    const ImVec4 surface0   = ImVec4(0.200f, 0.207f, 0.286f, 1.0f); // #313244
    const ImVec4 surface1   = ImVec4(0.247f, 0.254f, 0.337f, 1.0f); // #3f4056
    const ImVec4 surface2   = ImVec4(0.290f, 0.301f, 0.388f, 1.0f); // #4a4d63
    const ImVec4 overlay0   = ImVec4(0.396f, 0.403f, 0.486f, 1.0f); // #65677c
    const ImVec4 overlay2   = ImVec4(0.576f, 0.584f, 0.654f, 1.0f); // #9399b2
    const ImVec4 text       = ImVec4(0.803f, 0.815f, 0.878f, 1.0f); // #cdd6f4
    const ImVec4 subtext0   = ImVec4(0.639f, 0.658f, 0.764f, 1.0f); // #a3a8c3
    const ImVec4 mauve      = ImVec4(0.796f, 0.698f, 0.972f, 1.0f); // #cba6f7
    const ImVec4 peach      = ImVec4(0.980f, 0.709f, 0.572f, 1.0f); // #fab387
    const ImVec4 yellow     = ImVec4(0.980f, 0.913f, 0.596f, 1.0f); // #f9e2af
    const ImVec4 green      = ImVec4(0.650f, 0.890f, 0.631f, 1.0f); // #a6e3a1
    const ImVec4 teal       = ImVec4(0.580f, 0.886f, 0.819f, 1.0f); // #94e2d5
    const ImVec4 sapphire   = ImVec4(0.458f, 0.784f, 0.878f, 1.0f); // #74c7ec
    const ImVec4 blue       = ImVec4(0.533f, 0.698f, 0.976f, 1.0f); // #89b4fa
    const ImVec4 lavender   = ImVec4(0.709f, 0.764f, 0.980f, 1.0f); // #b4befe

    //Main window and backgrounds
    colors[ImGuiCol_WindowBg]             = base;
    colors[ImGuiCol_ChildBg]              = base;
    colors[ImGuiCol_PopupBg]              = surface0;
    colors[ImGuiCol_Border]               = surface1;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_FrameBg]              = surface0;
    colors[ImGuiCol_FrameBgHovered]       = surface1;
    colors[ImGuiCol_FrameBgActive]        = surface2;
    colors[ImGuiCol_TitleBg]              = mantle;
    colors[ImGuiCol_TitleBgActive]        = surface0;
    colors[ImGuiCol_TitleBgCollapsed]     = mantle;
    colors[ImGuiCol_MenuBarBg]            = mantle;
    colors[ImGuiCol_ScrollbarBg]          = surface0;
    colors[ImGuiCol_ScrollbarGrab]        = surface2;
    colors[ImGuiCol_ScrollbarGrabHovered] = overlay0;
    colors[ImGuiCol_ScrollbarGrabActive]  = overlay2;
    colors[ImGuiCol_CheckMark]            = green;
    colors[ImGuiCol_SliderGrab]           = sapphire;
    colors[ImGuiCol_SliderGrabActive]     = blue;
    colors[ImGuiCol_Button]               = surface0;
    colors[ImGuiCol_ButtonHovered]        = surface1;
    colors[ImGuiCol_ButtonActive]         = surface2;
    colors[ImGuiCol_Header]               = surface0;
    colors[ImGuiCol_HeaderHovered]        = surface1;
    colors[ImGuiCol_HeaderActive]         = surface2;
    colors[ImGuiCol_Separator]            = surface1;
    colors[ImGuiCol_SeparatorHovered]     = mauve;
    colors[ImGuiCol_SeparatorActive]      = mauve;
    colors[ImGuiCol_ResizeGrip]           = surface2;
    colors[ImGuiCol_ResizeGripHovered]    = mauve;
    colors[ImGuiCol_ResizeGripActive]     = mauve;
    colors[ImGuiCol_Tab]                  = surface0;
    colors[ImGuiCol_TabHovered]           = surface2;
    colors[ImGuiCol_TabActive]            = surface1;
    colors[ImGuiCol_TabUnfocused]         = surface0;
    colors[ImGuiCol_TabUnfocusedActive]   = surface1;
    colors[ImGuiCol_DockingPreview]       = sapphire;
    colors[ImGuiCol_DockingEmptyBg]       = base;
    colors[ImGuiCol_PlotLines]            = blue;
    colors[ImGuiCol_PlotLinesHovered]     = peach;
    colors[ImGuiCol_PlotHistogram]        = teal;
    colors[ImGuiCol_PlotHistogramHovered] = green;
    colors[ImGuiCol_TableHeaderBg]        = surface0;
    colors[ImGuiCol_TableBorderStrong]    = surface1;
    colors[ImGuiCol_TableBorderLight]     = surface0;
    colors[ImGuiCol_TableRowBg]           = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt]        = ImVec4(1.0f, 1.0f, 1.0f, 0.06f);
    colors[ImGuiCol_TextSelectedBg]       = surface2;
    colors[ImGuiCol_DragDropTarget]       = yellow;
    colors[ImGuiCol_NavHighlight]         = lavender;
    colors[ImGuiCol_NavWindowingHighlight]= ImVec4(1.0f, 1.0f, 1.0f, 0.7f);
    colors[ImGuiCol_NavWindowingDimBg]    = ImVec4(0.8f, 0.8f, 0.8f, 0.2f);
    colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
    colors[ImGuiCol_Text]                 = text;
    colors[ImGuiCol_TextDisabled]         = subtext0;

    //Rounded corners
    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;

    //Padding and spacing
    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(5.0f, 3.0f);
    style.ItemSpacing       = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
    style.IndentSpacing     = 21.0f;
    style.ScrollbarSize     = 14.0f;
    style.GrabMinSize       = 10.0f;

    //Borders
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBorderSize     = 0.0f;
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

void TsukiEngine::drawGeometry(VkCommandBuffer commandBuffer, VkImageView imageView) {
    _benchmarker.numDrawCalls = 0;
    _benchmarker.numTriangles = 0;
    auto start = std::chrono::system_clock::now();

    VkRenderingAttachmentInfo colorAttachment = tsukiinit::tAttachmentInfo(_drawImage.imageView, nullptr);
    VkRenderingAttachmentInfo depthAttachment = tsukiinit::tDepthAttachmentInfo(_depthImage.imageView, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingInfo renderInfo = tsukiinit::tRenderingInfo(_drawExtent, &colorAttachment, &depthAttachment);
    vkCmdBeginRendering(commandBuffer, &renderInfo);

    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width = _drawExtent.width;
    viewport.height = _drawExtent.height;
    viewport.minDepth = 0;
    viewport.maxDepth = 1;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset.x = 0;
    scissor.offset.y = 0;
    scissor.extent.width = _drawExtent.width;
    scissor.extent.height = _drawExtent.height;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    //Set 0) Scene data UBO
    AllocatedBuffer sceneDataBuffer = createBuffer(sizeof(SceneData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    getCurrFrame()._deletionQueue.push([=, this]() {
        destroyBuffer(sceneDataBuffer);
        });
    SceneData *sceneDataData = reinterpret_cast<SceneData *>(sceneDataBuffer.allocation->GetMappedData());
    *sceneDataData = sceneData;

    VkDescriptorSet sceneDescriptor = getCurrFrame()._frameDescriptors.allocate(_device, _sceneDataDescriptorLayout);
    {
        DescriptorWriter writer;
        writer.writeBuffer(0, sceneDataBuffer.buffer, sizeof(SceneData), 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
        writer.updateSet(_device, sceneDescriptor);
    }

    //Bind the two global descriptor sets
    VkDescriptorSet globalSets[] = { sceneDescriptor, _bindlessDescriptorSet };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        materialPipelines.opaquePipeline.layout, 0, 2, globalSets, 0, nullptr);

    //Draw loop
    auto draw = [&](const TsukiVulkanRender &renderable) {
        VkPipelineLayout layout = renderable.material->pipeline->layout;
        VkPipeline pipeline = renderable.material->pipeline->pipeline;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        //Bind Set 2) per-mesh vertex + material lookup buffers
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
            layout, 2, 1, &renderable.meshDescriptorSet, 0, nullptr);

        vkCmdBindIndexBuffer(commandBuffer, renderable.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        DrawPushConstants pushConstants;
        pushConstants.worldMatrix = renderable.transform;
        pushConstants.inverseTranspose = glm::mat4(glm::inverse(glm::transpose(glm::mat3(pushConstants.worldMatrix))));
        pushConstants.viewMode = viewMode;
        vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DrawPushConstants), &pushConstants);

        vkCmdDrawIndexed(commandBuffer, renderable.indexCount, 1, renderable.indexOffset, 0, 0);

        ++_benchmarker.numDrawCalls;
        _benchmarker.numTriangles += renderable.indexCount / 3;
        };

    for (auto &r : _mainDrawContext.opaqueObjects) {
        draw(r);
    }

    for (auto &r : _mainDrawContext.transparentObjects) {
        draw(r);
    }

    vkCmdEndRendering(commandBuffer);

    _mainDrawContext.opaqueObjects.clear();
    _mainDrawContext.transparentObjects.clear();

    auto end = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    _benchmarker.meshDrawTime = elapsed.count() / 1000.f;
}

void TsukiEngine::drawPathtraced(VkCommandBuffer commandBuffer) { //TODO
    _benchmarker.numDrawCalls = 0;
    _benchmarker.numTriangles = 0;
    auto start = std::chrono::system_clock::now();

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pathtracerPipeline);
    //Bind the descriptor set with the draw image for the compute pass
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, _pathtracerPipelineLayout, 0, 1, &_drawImageDescriptors, 0, nullptr);

    //Push the, er, push constants for the tone mapping type
    vkCmdPushConstants(commandBuffer, _pathtracerPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ToneMapMode), & _toneMapMode);

    //Dispatch
    vkCmdDispatch(commandBuffer, std::ceil(_drawExtent.width / 32.f), std::ceil(_drawExtent.height / 32.f), 1); //Like a kernel launch

    auto end = std::chrono::system_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    _benchmarker.meshDrawTime = elapsed.count() / 1000.f;
}

void TsukiEngine::initMeshPipeline() {
    VkShaderModule meshShader;
    if (!tsukiutil::loadShaderModule("./shaders/meshshader.spv", _device, &meshShader)) {
        std::cerr << "Error building mesh shader" << std::endl;
    }

    VkPushConstantRange bufferRange{};
    bufferRange.offset = 0;
    bufferRange.size = sizeof(DrawPushConstants);
    bufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayout layouts[] = {
        _sceneDataDescriptorLayout,
        _bindlessTextureDescriptorLayout,
        _perMeshDescriptorLayout
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = tsukiinit::tPipelineLayoutCreateInfo();
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &bufferRange;
    pipelineLayoutInfo.setLayoutCount = 3;
    pipelineLayoutInfo.pSetLayouts = layouts;

    VK_CHECK(vkCreatePipelineLayout(_device, &pipelineLayoutInfo, nullptr, &_meshPipelineLayout));

    PipelineBuilder pipelineBuilder;
    pipelineBuilder._pipelineLayout = _meshPipelineLayout;
    pipelineBuilder.setShaders(meshShader, meshShader, "vertMain", "fragMain");
    pipelineBuilder.setInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder.setPolygonMode(VK_POLYGON_MODE_FILL);
    pipelineBuilder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
    pipelineBuilder.setMultisamplingNone();
    pipelineBuilder.disableBlending();
    pipelineBuilder.enableDepthTest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

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
            pixels[y * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : black;
        }
    }
    _errorCheckerboardImage = createImage(pixels.data(), VkExtent3D{ 16, 16, 1 }, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_SAMPLED_BIT);

    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
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

    defaultData.passType = TsukiMaterialPass::TSUKI_MATERIAL_OPAQUE;
    defaultData.pipeline = &materialPipelines.opaquePipeline;
}
//End Chapter 3

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
    vk12.descriptorIndexing = VK_TRUE;
    vk12.storageBuffer8BitAccess = VK_TRUE;
    vk12.uniformAndStorageBuffer8BitAccess = VK_TRUE;
    vk12.shaderInt8 = VK_TRUE;
    vk12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    vk12.descriptorBindingPartiallyBound = VK_TRUE;
    vk12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    vk12.runtimeDescriptorArray = VK_TRUE;

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
    //5) Is a discrete GPU with manufacturer NVIDIA

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
        vk12.descriptorIndexing &&
        vk12.storageBuffer8BitAccess &&
        vk12.uniformAndStorageBuffer8BitAccess &&
        vk12.shaderInt8 &&
        vk12.shaderSampledImageArrayNonUniformIndexing &&
        vk12.descriptorBindingPartiallyBound &&
        vk12.descriptorBindingSampledImageUpdateAfterBind &&
        vk12.runtimeDescriptorArray &&
        vk13.synchronization2 &&
        vk13.dynamicRendering &&
        eds.extendedDynamicState;
    
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(device, &physicalDeviceProperties);
    bool isNvidia = physicalDeviceProperties.vendorID == 4318;
    bool isDiscrete = physicalDeviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

    return indices.isComplete() && extensionsSupported && swapChainAdequate && supportsRequiredFeatures && isNvidia && isDiscrete;
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
        if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            return availablePresentMode;
        }
        /*if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }*/
    }

    //FIFO is the only present mode guaranteed to be available
    return VK_PRESENT_MODE_FIFO_KHR;
}
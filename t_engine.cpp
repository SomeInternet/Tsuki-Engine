#include "t_engine.h"
#include "t_initializers.h"
#include "t_types.h"
#include "t_images.h"

#include <chrono>
#include <thread>
#include <iostream>
#include <set>

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
        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME
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
        }

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
	//TODO (-)
    VK_CHECK(vkWaitForFences(_device, 1, &getCurrFrame()._renderFence, true, 1000000000)); //Wait for 1 fence (the fence of the current frame) for up to 1 second

    //Get image from swapchain
    uint32_t swapChainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(_device, _swapChain, 1000000000, getCurrFrame()._swapChainSemaphore, nullptr, &swapChainImageIndex)); //Signals the swapchain semaphore when complete

    VK_CHECK(vkResetFences(_device, 1, &getCurrFrame()._renderFence)); //Reset the fence

    //Begin command buffer
    VkCommandBuffer commandBuffer = getCurrFrame()._mainCommandBuffer;
    VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));
    VkCommandBufferBeginInfo commandBufferBeginInfo = tsukiinit::tCommandBufferBeginInfo(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT); //1-time command buffer (we re-record each frame)
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

    //TODO: Record commands
    //Transition image to writeable format
    tsukiutil::transitionImageLayout(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    //Clear the image color
    VkClearColorValue clearValue;
    float flash = std::abs(std::sin(_frameNum / 120.f));
    clearValue = { { 0.0f, 0.0f, flash, 1.0f } };
    VkImageSubresourceRange clearRange = tsukiinit::tImageSubresourceRange(VK_IMAGE_ASPECT_COLOR_BIT);
    vkCmdClearColorImage(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

    //Transition image to presentable format
    tsukiutil::transitionImageLayout(commandBuffer, _swapChainImages[swapChainImageIndex], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    //TODO: End command buffer
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    //TODO: Submit the command buffer to the queue
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

		draw();
	}
}

void TsukiEngine::initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
}

void TsukiEngine::initSwapChain() { //NOTE: Watch out for window resizes later...
    createSwapChain(WIDTH, HEIGHT);
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
}

void TsukiEngine::initSyncStructures() {
    //TODO: Create synchronization structures
    VkFenceCreateInfo fenceCreateInfo = tsukiinit::tFenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT); //Create as pre-signalled, so we don't block immediately
    VkSemaphoreCreateInfo semaphoreCreateInfo = tsukiinit::tSemaphoreCreateInfo();

    for (int i = 0; i < _swapChainImages.size(); ++i) {
        VkSemaphore semaphore;
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &semaphore));
        _renderSemaphores.push_back(semaphore);
    }

    for (int i = 0; i < FRAMES_IN_FLIGHT; ++i) {
        VK_CHECK(vkCreateSemaphore(_device, &semaphoreCreateInfo, nullptr, &_frames[i]._swapChainSemaphore));

        VK_CHECK(vkCreateFence(_device, &fenceCreateInfo, nullptr, &_frames[i]._renderFence));
    }
}

//HELPERS
//===================================================================================================================
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
}

void TsukiEngine::destroySwapChain() {
    vkDestroySwapchainKHR(_device, _swapChain, nullptr);
    for (int i = 0; i < _swapChainImageViews.size(); ++i) { //TODO: Optimize?
        vkDestroyImageView(_device, _swapChainImageViews[i], nullptr);
    }
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

    VkPhysicalDeviceVulkan13Features vk13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    vk13.synchronization2 = VK_TRUE;
    vk13.dynamicRendering = VK_TRUE;

    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT };
    eds.extendedDynamicState = VK_TRUE;

    features2.pNext = &vk11;
    vk11.pNext = &vk13;
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
    VkPhysicalDeviceVulkan13Features vk13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
    VkPhysicalDeviceExtendedDynamicStateFeaturesEXT eds = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT };

    supportedFeatures2.pNext = &vk11;
    vk11.pNext = &vk13;
    vk13.pNext = &eds;

    vkGetPhysicalDeviceFeatures2(device, &supportedFeatures2);

    bool supportsRequiredFeatures =
        supportedFeatures2.features.samplerAnisotropy &&
        vk11.shaderDrawParameters &&
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
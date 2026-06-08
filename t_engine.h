#pragma once
#include "t_types.h"

#define WIDTH 1700
#define HEIGHT 900

class TsukiEngine {
public:

	bool _isInit{ false };
	int _frameNum{ 0 };
	bool stopRendering{ false };
	VkExtent2D _windowExtent{ WIDTH, HEIGHT }; //TODO: Change to match...

	GLFWwindow *window{ nullptr };

	FrameData _frames[FRAMES_IN_FLIGHT];

	//Vulkan handles
	VkInstance _instance; //Vulkan library handle
	VkDebugUtilsMessengerEXT _debugMessenger; //Debug output handle
	VkPhysicalDevice _physicalDevice; //Physical GPU
	VkDevice _device; //Logical device
	VkSurfaceKHR _surface; //Vulkan surface

	//Swapchain handles
	VkSwapchainKHR _swapChain;
	VkFormat _swapChainImageFormat;
	std::vector<VkImage> _swapChainImages;
	std::vector<VkImageView> _swapChainImageViews;
	VkExtent2D _swapChainExtent;

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	static TsukiEngine &Get();

	void init();
	void cleanup();
	void draw(); //Draw loop
	void run(); //Run main loop

	FrameData &getCurrFrame() { return _frames[_frameNum % FRAMES_IN_FLIGHT]; }

private:

	void initVulkan();
	void initSwapChain();
	void initCommands();
	void initSyncStructures();

	void createSwapChain(uint32_t width, uint32_t height);
	void destroySwapChain();

	//VULKAN SETUP (ADAPTED FROM VULKAN-TUTORIAL)
	//===================================================================================================================
	void createInstance();
	void setupDebugMessenger();
	void createSurface();
	void pickPhysicalDevice();
	void createLogicalDevice();

	//VULKAN SETUP HELPERS
	//===================================================================================================================
	bool checkValidationLayerSupport();
	std::vector<const char *> getInstanceExtensions();
	VkResult createDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger);
	void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks *pAllocator);
	bool isDeviceSuitable(VkPhysicalDevice device);
	bool checkDeviceExtensionSupport(VkPhysicalDevice device);
	QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);

	SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
	VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats);
	VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes);
};

#pragma once
#include "t_types.h"
#include "t_descriptors.h"

#define WIDTH 1700
#define HEIGHT 900

constexpr unsigned int FRAMES_IN_FLIGHT = 2;

class TsukiEngine {
public:

	bool _isInit{ false };
	int _frameNum{ 0 };
	bool stopRendering{ false };
	VkExtent2D _windowExtent{ WIDTH, HEIGHT }; //TODO: Change to match...

	GLFWwindow *window{ nullptr };

	FrameData _frames[FRAMES_IN_FLIGHT];
	//To avoid validation layer warnings
	std::vector<VkSemaphore> _renderSemaphores; //Have presentation wait on the rendering being finished
	//TODO: Understand this fix a little better

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

	//Chapter 2
	AllocatedImage _drawImage;
	VkExtent2D _drawExtent;

	DescriptorAllocator globalDescriptorAllocator;

	VkDescriptorSet _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	VkPipeline _gradientPipeline;
	VkPipelineLayout _gradientPipelineLayout;

	//IMGUI
	VkFence _immFence;
	VkCommandBuffer _immCommandBuffer;
	VkCommandPool _immCommandPool;

	//Additional helper members
	DeletionQueue _mainDeletionQueue;
	VmaAllocator _allocator;

	static TsukiEngine &Get();
	FrameData &getCurrFrame() { return _frames[_frameNum % FRAMES_IN_FLIGHT]; }

	//MAIN FUNCTIONS
	//===================================================================================================================
	void init();
	void cleanup();
	void draw(); //Draw loop
	void run(); //Run main loop

	//PUBLIC HELPERS
	//===================================================================================================================
	void drawBackground(VkCommandBuffer commandBuffer, uint32_t swapChainImageIndex);

	void immediateSubmit(std::function<void(VkCommandBuffer commandBuffer)> &&function);

private:

	//PRIVATE HELPERS
	//===================================================================================================================
	void initVulkan();
	void initSwapChain();
	void initCommands();
	void initSyncStructures();

	void createSwapChain(uint32_t width, uint32_t height);
	void destroySwapChain();

	void initDescriptors();

	void initPipelines();
	void initBackgroundPipelines();

	void initImgui();
	void drawImgui(VkCommandBuffer commandBuffer, VkImageView targetImageView);

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

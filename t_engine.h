#pragma once
#include "t_types.h"

#define WIDTH 1700
#define HEIGHT 900

class TsukiEngine {
public:

	bool _isInit{ false };
	int _frame{ 0 };
	bool stopRendering{ false };
	VkExtent2D _windowExtent{ WIDTH, HEIGHT }; //TODO: Change to match...

	GLFWwindow *window{ nullptr };

	static TsukiEngine& Get();

	void init();
	void cleanup();
	void draw(); //Draw loop
	void run(); //Run main loop

	//Vulkan handles
	VkInstance _instance; //Vulkan library handle
	VkDebugUtilsMessengerEXT _debugMessenger; //Debug output handle
	VkPhysicalDevice _physicalDevice; //Physical GPU
	VkDevice _device; //Logical device
	VkSurfaceKHR _surface; //Vulkan surface

private:

	void initVulkan();
	void initSwapChain();
	void initCommands();
	void initSyncStructures();

	//Vulkan setup (from Vulkan Tutorial)
	void createInstance();
	void setupDebugMessenger();
	void createSurface();
	void pickPhysicalDevice();
	void createLogicalDevice();

	//Vulkan setup helpers
	bool checkValidationLayerSupport();
	std::vector<const char *> getInstanceExtensions();
	VkResult createDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo, const VkAllocationCallbacks *pAllocator, VkDebugUtilsMessengerEXT *pDebugMessenger);
	bool isDeviceSuitable(VkPhysicalDevice device);
	bool checkDeviceExtensionSupport(VkPhysicalDevice device);
	QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
	SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
};

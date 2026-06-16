#pragma once
#include "t_types.h"
#include "t_loader.h"
#include "t_descriptors.h"

#define WIDTH 1700
#define HEIGHT 900

constexpr unsigned int FRAMES_IN_FLIGHT = 2;

struct ComputePushConstants { //TODO: Check device specs to see limits of how much can be pushed
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
	glm::vec4 data4;
};

struct ComputeEffect {
	const char *name;

	VkPipeline pipeline;
	VkPipelineLayout layout;

	ComputePushConstants data;
};

struct FrameData {
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	VkSemaphore _swapChainSemaphore; //Have the rendering commands wait on receiving the image from the swapchain
	VkFence _renderFence; //Have command buffer recording wait on rendering being finished

	DeletionQueue _deletionQueue;

	DynamicDescriptorAllocator _frameDescriptors;
};

class TsukiEngine {
public:

	bool _isInit{ false };
	int _frameNum{ 0 };
	bool _render{ true };
	bool _resize{ false }; //Resize boolean flag
	VkExtent2D _windowExtent{ WIDTH, HEIGHT }; //TODO: Change to match...

	GLFWwindow *_window{ nullptr };

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
	float renderScale{ 1.f };

	DescriptorAllocator globalDescriptorAllocator;

	VkDescriptorSet _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	VkPipeline _gradientPipeline;
	VkPipelineLayout _gradientPipelineLayout;

	std::vector<ComputeEffect> backgroundEffects;
	int currBackgroundEffect{ 0 };

	//Chapter 3
	VkPipelineLayout _trianglePipelineLayout;
	VkPipeline _trianglePipeline;

	VkPipelineLayout _meshPipelineLayout;
	VkPipeline _meshPipeline;

	GPUMeshBuffers rectangle;

	std::vector<std::shared_ptr<Mesh>> testMeshes;

	AllocatedImage _depthImage;
	//

	//Chapter 4
	SceneData sceneData;
	VkDescriptorSetLayout _sceneDataDescriptorLayout;

	AllocatedImage _whiteImage;
	AllocatedImage _blackImage;
	AllocatedImage _grayImage;
	AllocatedImage _errorCheckerboardImage;

	VkSampler _defaultSamplerLinear;
	VkSampler _defaultSamplerNearest;

	VkDescriptorSetLayout _singleImageDescriptorLayout;
	//

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

	//MESH HELPERS
	//===================================================================================================================
	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

private:

	//PRIVATE HELPERS
	//===================================================================================================================
	void initVulkan();
	void initSwapChain();
	void initCommands();
	void initSyncStructures();

	void createSwapChain(uint32_t width, uint32_t height);
	void destroySwapChain();
	void resizeSwapChain();

	void initDescriptors();

	void initPipelines();
	void initBackgroundPipelines();

	void initImgui();
	void drawImgui(VkCommandBuffer commandBuffer, VkImageView targetImageView);

	//Chapter 3
	void initTrianglePipeline();
	void drawGeometry(VkCommandBuffer commandBuffer);

	void initMeshPipeline();
	void initDefaultMeshData();
	//

	//Chapter 4
	AllocatedImage createImage(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, bool mipmaps = false);
	AllocatedImage createImage(void *data, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, bool mipmaps = false);
	void destroyImage(const AllocatedImage &image);
	//

	//TODO: Move this elswhere?
	AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	void destroyBuffer(const AllocatedBuffer &buffer);

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

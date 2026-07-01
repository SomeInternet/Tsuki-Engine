#pragma once
#include "t_types.h"
#include "t_loader.h"
#include "t_descriptors.h"
#include "t_input.h"
#include "t_camera.h"
#include "t_cudacommon.h"
#include "t_bvh.h"

#include <unordered_map>

#define WIDTH 1700
#define HEIGHT 900

#define CLEAR_VALUE 0
#define COMPUTE_BACKGROUND 0
#define CUDA_TEST 1

constexpr unsigned int FRAMES_IN_FLIGHT = 2;

//WINDOWS SECURITY ATTRIBUTES
//===================================================================================================================
class WindowsSecurityAttributes
{
protected:
	SECURITY_ATTRIBUTES  m_winSecurityAttributes;
	PSECURITY_DESCRIPTOR m_winPSecurityDescriptor;

public:
	WindowsSecurityAttributes();
	SECURITY_ATTRIBUTES *operator&();
	~WindowsSecurityAttributes();
};

//ACTUAL GPU STUFF
//===================================================================================================================
struct FrameData {
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	VkSemaphore _swapChainSemaphore; //Have the rendering commands wait on receiving the image from the swapchain
	VkFence _renderFence; //Have command buffer recording wait on rendering being finished

	DeletionQueue _deletionQueue;

	DynamicDescriptorAllocator _frameDescriptors;
};

struct Benchmarker {
	float frameTime;
	int numTriangles;
	int numDrawCalls;
	float sceneUpdateTime;
	float meshDrawTime;
};

//MATERIALS
//===================================================================================================================
struct MaterialTexture {
	AllocatedImage image;
	VkSampler sampler;
};

struct TsukiMaterialResources { //TODO
	AllocatedImage colorImage;
	VkSampler colorSampler;

	AllocatedImage metallicRoughnessImage;
	VkSampler metallicRoughnessSampler;

	//TODO: PROPERLY INTEGRATE
	AllocatedImage emissionFacImage;
	VkSampler emissionFacSampler;

	AllocatedImage normalImage;
	VkSampler normalSampler;
	//

	VkBuffer dataBuffer;
	uint32_t dataBufferOffset;
};

struct TsukiMaterialPassPipelines {
	TsukiMaterialPipeline opaquePipeline;
	TsukiMaterialPipeline transparentPipeline;

	void buildPipelines(TsukiEngine *engine);
	void clearResources(VkDevice device);
};

//SCENEGRAPH
//===================================================================================================================
struct TMeshNode : public TNode {
	std::shared_ptr<TsukiMesh> mesh;

	bool loadedToTlas{ false };
	uint32_t blasId{ 0 };

	virtual void queueDraw(const glm::mat4 &matrix, TsukiDrawContext &context) override;
};

struct TsukiVulkanRender {
	VkBuffer indexBuffer;
	glm::mat4 transform;
	VkDescriptorSet meshDescriptorSet;
	uint32_t indexCount;
	uint32_t indexOffset;
	TsukiMaterial *material;
};

class TsukiEngine;

struct TsukiDrawContext {
	TsukiEngine *engine;
	std::vector<TsukiVulkanRender> opaqueObjects;
	std::vector<TsukiVulkanRender> transparentObjects;
};

enum class TsukiRenderMode : uint8_t {
	TSUKI_RENDER_MODE_VIEWPORT_FORWARD, TSUKI_RENDER_MODE_PATHTRACED_CUDA
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

	TsukiInput input;
	TsukiCamera camera;

	//Chapter 2
	AllocatedImage _drawImage;
	VkExtent2D _drawExtent;
	float renderScale{ 1.f };

	DynamicDescriptorAllocator globalDescriptorAllocator;

	VkDescriptorSet _drawImageDescriptors;
	VkDescriptorSetLayout _drawImageDescriptorLayout;

	VkPipeline _pathtracerPipeline;
	VkPipelineLayout _pathtracerPipelineLayout;

	//Chapter 3
	VkPipelineLayout _meshPipelineLayout;
	VkPipeline _meshPipeline;

	AllocatedImage _depthImage;
	//

	//Chapter 4
	SceneData sceneData;
	VkDescriptorSetLayout _sceneDataDescriptorLayout;

	AllocatedImage _whiteImage;
	AllocatedImage _blackImage;
	AllocatedImage _grayImage;
	AllocatedImage _errorCheckerboardImage;

	VkSampler _defaultSamplerLinear; //Sampling with linear blending
	VkSampler _defaultSamplerNearest; //Sampling with nearest blending (good for low-res textures you don't want to end up blurry)

	VkDescriptorSetLayout _perMeshDescriptorLayout;    // Set 2)vertex + material lookup buffers
	DynamicDescriptorAllocator _meshDescriptorAllocator; //Persistent allocator for per-mesh descriptor sets

	AllocatedBuffer _materialDataBuffer; //GPU buffer holding TsukiMaterialData array

	TsukiMaterial defaultData;
	TsukiMaterialPassPipelines materialPipelines;

	TsukiDrawContext _mainDrawContext;
	//

	//Chapter 5
	ViewMode viewMode{ ViewMode::VIEW_COLOR };

	std::unordered_map<std::string, std::shared_ptr<TsukiGLTF>> loadedScenes;

	Benchmarker _benchmarker;
	//

	//BETTER GLTF LOADING
	std::vector<std::shared_ptr<TsukiMesh>> meshes;
	std::vector<TsukiMaterialData> materials; //One big global material buffer
	std::vector<AllocatedImage> materialTextures; //References the textures, but the glTF object owns them

	//BINDLESS MATERIAL DESCRIPTORS
	VkDescriptorSet _bindlessDescriptorSet;
	VkDescriptorSetLayout _bindlessTextureDescriptorLayout; //Set 1) material data + textures + sampler
	DynamicDescriptorAllocator _bindlessDescriptorAllocator;

	//ACCELERATION STRUCTURES
	std::vector<std::vector<BVHNode>> _blas;
	std::vector<BLASInstance> _sceneInstances;
	std::vector<TLASNode> _tlas;
	std::vector<glm::mat4> _tlasTransforms;
	std::vector<glm::mat4> _tlasInvTransforms;
	bool _tlasDirty{ false };

	//CUDA INTEROP
	VkExportMemoryAllocateInfo exportInfo;
	VmaPool _vmaExternalPool; //Pool for allocating memory that will be exported

	AllocatedBuffer cudaImageBuffer;
	VkSemaphore cudaWriteToBufferSemaphore;
	VkSemaphore cudaRenderDoneSemaphore;
	TsukiCudaData cudaData;
	TsukiCudaAccelerationStructures hostAS{};
	std::vector<BVHNode *> blasPtrs; //Host-side copy of the per-mesh BVH device pointers (hostAS.d_bvh lives on the device)

	TsukiRenderMode _renderMode{ TsukiRenderMode::TSUKI_RENDER_MODE_VIEWPORT_FORWARD };

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
	void drawBackground(VkCommandBuffer commandBuffer, VkImage image);

	void immediateSubmit(std::function<void(VkCommandBuffer commandBuffer)> &&function);

	//Chapter 4
	AllocatedImage createImage(VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, bool mipmaps = false);
	AllocatedImage createImage(void *data, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, bool mipmaps = false);
	void destroyImage(const AllocatedImage &image);
	void updateScene();
	//

	//TODO: Move this elswhere?
	AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	AllocatedBuffer createExternalBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
	AllocatedImage createExternalImage(void *data, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, bool mipmaps = false);
	void destroyBuffer(const AllocatedBuffer &buffer);

	//MESH HELPERS
	//===================================================================================================================
	DeviceMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<glm::vec4> positions, std::span<glm::vec4> normals, std::span<glm::vec4> colors, std::span<glm::vec2> uvs, std::span<uint32_t> materialLookup, int startMaterialIndex);

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

	//PRIVATE HELPERS
	//===================================================================================================================
	void initCudaData();
	void uploadCudaSceneData();
	void freeCudaSceneData();

	void initImgui();
	void initImguiTheme();
	void drawImgui(VkCommandBuffer commandBuffer, VkImageView targetImageView);

	//Chapter 3
	void drawGeometry(VkCommandBuffer commandBuffer, VkImageView imageView);
	void drawPathtraced(VkCommandBuffer commandBuffer);

	void initMeshPipeline();
	void initDefaultMeshData();
	//

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

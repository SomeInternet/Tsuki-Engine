#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>
#include <iostream>

#define NOMINMAX //Stop windows.h definitions of min and max
#define VK_USE_PLATFORM_WIN32_KHR //Allows us to enable KHR external memory win32
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>

#include <vk_mem_alloc.h>

#include "t_cudacommon.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

//The do-while helps w/ expansion, and scopes the err
//TODO: Either fix the fmt stuff, or just use std::cout or std::cerr
#define VK_CHECK(x)																																\
	do {																																		\
		VkResult err = x;																														\
		if (err) {																																\
			std::cerr << "Vulkan error at file: " << __FILE__ << ", line: " << __LINE__ << ", error: " << string_VkResult(err) << std::endl;	\
			abort();																															\
		}																																		\
	} while (0)

#define EPSILON .0001f

struct QueueFamilyIndices {
	std::optional<uint32_t> graphicsFamily;
	std::optional<uint32_t> presentFamily;
	//Note that transfer operations are implicitly supported by any queue family that supports graphics or compute operations

	bool isComplete() {
		return (graphicsFamily.has_value() && presentFamily.has_value());
	}
};

struct SwapChainSupportDetails {
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};

struct DeletionQueue { //TODO: Optimize? (https://vkguide.dev/docs/new_chapter_2/vulkan_new_rendering/)
	std::deque<std::function<void()>> deletors;

	void push(std::function<void()> &&function) {
		deletors.push_back(function);
	}

	void flush() {
		for (auto it = deletors.rbegin(); it != deletors.rend(); ++it) {
			(*it)();
		}

		deletors.clear();
	}
};

struct AllocatedImage {
	VkImage image;
	VkImageView imageView;
	VmaAllocation allocation;
	VkExtent3D imageExtent;
	VkFormat imageFormat;
};

struct AllocatedBuffer {
	VkBuffer buffer; //Vulkan handle to the buffer
	VmaAllocation allocation;
	VmaAllocationInfo info;
};

constexpr uint32_t MAX_BINDLESS_TEXTURES = 4096;

struct DeviceMeshBuffers {
	AllocatedBuffer indexBuffer;
	AllocatedBuffer posBuffer;
	AllocatedBuffer normalBuffer;
	AllocatedBuffer colorBuffer;
	AllocatedBuffer uvBuffer;
	AllocatedBuffer materialLookupBuffer;

	VkDescriptorSet perMeshDescriptorSet;
};

enum ViewMode {
	VIEW_SOLID, VIEW_COLOR, VIEW_NORMAL
};

enum ToneMapMode {
	TONE_REINHARD, TONE_ACES, TONE_AGX
};

struct DrawPushConstants {
	glm::mat4 worldMatrix;
	glm::mat4 inverseTranspose;
	ViewMode viewMode;
};

//
struct SceneData { //TODO: Update
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewproj;

	glm::vec3 camPos;

	float ambient;
};

//MATERIALS
//===================================================================================================================
//Contains handles for the pipeline objects of a material
struct TsukiMaterialPipeline {
	VkPipeline pipeline;
	VkPipelineLayout layout;
};

//Contains the material pipeline handles for selecting opaque/transparent paths
struct TsukiMaterial {
	TsukiMaterialPipeline *pipeline;
	TsukiMaterialPass passType;
};

//OBJECTS AND RENDERABLES
//===================================================================================================================
struct TsukiDrawContext; //Forward declaration

class TRenderable {
	virtual void queueDraw(const glm::mat4 &matrix, TsukiDrawContext &context) = 0;
};

struct TNode : public TRenderable {
	std::weak_ptr<TNode> parent; //A weak pointer is a smart pointer that doesn't claim ownership over the resource pointed to
	//(i.e. it doesn't extend its lifespan)

	std::vector<std::shared_ptr<TNode>> children; //Child nodes in the scene graph. Drawn recursively when the parent is drawn

	glm::mat4 localTransform;
	glm::mat4 worldTransform;

	void updateTransform(const glm::mat4 &parentWorldTransform) {
		worldTransform = parentWorldTransform * localTransform;
		for (auto child : children) {
			child->updateTransform(worldTransform);
		}
	}

	virtual void queueDraw(const glm::mat4 &matrix, TsukiDrawContext &context) {
		for (auto &child : children) {
			child->queueDraw(matrix, context);
		}
	}
};

//REVIEW:
//Command pools are thread-isolated pieces of memory corresponding to a queue family.
//We allocated command buffers from them, which can be submitted to a queue in the queue family
//of the command pool

//Descriptor Set Layouts "promise" the Vulkan pipeline what kinds of resources
//will be available at what stage of the pipeline. It groupts descriptor set layout bindings

//Extensions fall into 2 categories: instance and device. Instance extensions enable Vulkan capabilities independent of the physical device

//A Blit is a GPU-accelerated operation for pixel data transfer from 1 image to another

//NOTES:
//Spans are like views into arrays. They denote a contiguous region of memory hosting a certain number elements of a certain type

//Descriptor set layouts are like a class. They tell us how a descriptor set will be laid out. A descriptor set is sort of like
//an instance of that class. A descriptor is a pointer to GPU data accompanied by metadata. A pipeline layout tells the pipeline
//what kinds of resources will be available and when

//Blending occurs at the end of the graphics pipeline. It determines how our fragments interact with what is already in the attachment
//Alpha and color blending are separated into 2 operations. We can blend multiplicatively, additively, etc. through VK_BLEND_OP_XXX
//We can set the factor w/ VK_BLEND_FACTOR (e.g. VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA)

//When a descriptor pool is created, it is given a maximum budget of sets and different kinds of descriptors. Sets can be allocated from the pool as
//the programmer sees fit until the budget can't accommodate it anymore

//std::vectors can be implicitly converted to std::spans

//The advantage that weak pointers have over raw pointers is protection against dangling
//To read or modify the data, you must temporarily promote the weak pointer to a shared pointer via .lock()
//.lock() returns nullptr if the resource pointed to no longer exists
//You can therefore use .lock() to check if the resource is still alive

//glTF's (glb's, at least) store the base material colors in the material constants, in this implementation

//Storage buffers can be bigger than uniform buffers

//FOR GPU-side debugging: Put this into debugger:
//C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.2\compute-sanitizer\compute-sanitizer.exe
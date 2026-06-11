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

#include <fmt/core.h>
#include <fmt/format.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

//The do-while helps w/ expansion, and scopes the err
//TODO: Either fix the fmt stuff, or just use std::cout or std::cerr
#define VK_CHECK(x)																\
	do {																		\
		VkResult err = x;														\
		if (err) {																\
			std::cerr << "Vulkan error: " << string_VkResult(err) << std::endl;	\
			abort();															\
		}																		\
	} while (0)

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

struct FrameData {
	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;

	VkSemaphore _swapChainSemaphore; //Have the rendering commands wait on receiving the image from the swapchain
	VkFence _renderFence; //Have command buffer recording wait on rendering being finished

	DeletionQueue _deletionQueue;
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

struct Vertex {
	//Structured for efficient memory alignment
	//Apparently, GPU's like aligning data structures to 4 byte slots
	glm::vec3 pos;
	float uvX;
	glm::vec3 normal;
	float uvY;
	glm::vec4 color;
};

struct GPUMeshBuffers {
	AllocatedBuffer indexBuffer;
	AllocatedBuffer vertexBuffer;
	VkDeviceAddress vertexBufferAddress;
};

struct GPUDrawPushConstants {
	glm::mat4 worldMatrix;
	VkDeviceAddress vertexBuffer;
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
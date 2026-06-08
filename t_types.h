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

#define VK_USE_PLATFORM_WIN32_KHR //Allows us to enable KHR external memory win32
#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>

#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

//The do-while helps w/ expansion, and scopes the err.
#define VK_CHECK(x)													\
	do {															\
		VkResult err = x;											\
		if (err) {													\
			fmt::println("Vulkan error: {}", string_VkResult(err));	\
			abort();												\
		}															\
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

struct FrameData {

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;
};

constexpr unsigned int FRAMES_IN_FLIGHT = 2;

//REVIEW:
//Command pools are thread-isolated pieces of memory corresponding to a queue family.
//We allocated command buffers from them, which can be submitted to a queue in the queue family
//of the command pool

//Descriptor Set Layouts "promise" the Vulkan pipeline what kinds of resources
//will be available at what stage of the pipeline. It groupts descriptor set layout bindings

//Extensions fall into 2 categories: instance and device. Instance extensions enable Vulkan capabilities independent of the physical device
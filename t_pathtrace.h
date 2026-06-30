#pragma once

#include "t_cudacommon.h"

#include <device_launch_parameters.h>
#define NOMINMAX //Stop windows.h definitions of min and max
#define VK_USE_PLATFORM_WIN32_KHR //Allows us to enable KHR external memory win32
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>

#define GLM_FORCE_CUDA 
#define GLM_FORCE_RADIANS 
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace tsukicudapathtrace {
	void testImage(TsukiCudaData *cudaData, int width, int height);

	void testRaytrace(TsukiCudaData *cudaData, int width, int height);
};
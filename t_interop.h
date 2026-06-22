#pragma once
#include <cuda_runtime.h>
#include <vulkan/vulkan_core.h>

class TsukiEngine;

namespace tsukiutil {
	HANDLE getVkSemaphoreHandle(TsukiEngine *engine, VkSemaphore *semaphore);
	void getCudaSemaphore(TsukiEngine *engine, cudaExternalSemaphore_t *cudaSemaphore, VkSemaphore *vkSemaphore);

	//Okay, since VkGuide already uses VkDeviceAddresses I don't think I actually need this
	HANDLE getVkMemoryHandle(TsukiEngine *engine, VkDeviceMemory *memory);
	void getCudaExternalMemory(TsukiEngine *engine, void **devicePointer, cudaExternalMemory_t *cudaMemory, VkDeviceMemory *vkMemory, VkDeviceSize size);
};
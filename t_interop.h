#pragma once
#include <cuda_runtime.h>

#include "t_types.h"

namespace tsukiutil {
	HANDLE getVkSemaphoreHandle(TsukiEngine *engine, VkSemaphore &semaphore);
	HANDLE getVkMemoryHandle(VkDeviceMemory &memory);

	void getCudaSemaphore(TsukiEngine *engine, cudaExternalSemaphore_t &cudaSemaphore, VkSemaphore &vkSemaphore);
	//Okay, since VkGuide already uses VkDeviceAddresses I don't think I actually need this
	void getCudaExternalMemory(void **devicePointer, cudaExternalMemory_t &cudaMemory, VkDeviceMemory &vkMemory, VkDeviceSize size);
};
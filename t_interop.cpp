#include "t_cudacommon.h"
#include "t_engine.h"
#include "t_interop.h"

//From (https://github.com/NVIDIA/cuda-samples/blob/master/cpp/5_Domain_Specific/vulkanImageCUDA/vulkanImageCUDA.cu)

//TODO: Understand what this is doing lol
HANDLE tsukiutil::getVkSemaphoreHandle(TsukiEngine *engine, VkSemaphore *semaphore) {
	//https://github.com/NVIDIA/cuda-samples/blob/master/cpp/5_Domain_Specific/simpleVulkan/VulkanBaseApp.cpp : getSemaphoreHandle
	//NOTE: HANDLE is a Windows OS-level pointer to an existing Vulkan semaphore. It's also an alias for a void *
	HANDLE handle;

	//Configure a request to the kernel to extract the pointer for the resource and wrap it in a handle that other APIs can use
	VkSemaphoreGetWin32HandleInfoKHR semaphoreGetHandleInfo{};
	semaphoreGetHandleInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
	semaphoreGetHandleInfo.pNext = nullptr;
	semaphoreGetHandleInfo.semaphore = *semaphore;
	semaphoreGetHandleInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

	//Get a pointer to the platform-specific memory address of the function
	PFN_vkGetSemaphoreWin32HandleKHR getSemaphoreHandleFun{};
	getSemaphoreHandleFun = (PFN_vkGetSemaphoreWin32HandleKHR)vkGetDeviceProcAddr(engine->_device, "vkGetSemaphoreWin32HandleKHR");
	if (!getSemaphoreHandleFun) {
		std::cerr << "Failed to retrieve vkGetMemoryWin32HandleKHR!" << std::endl;
		abort();
	}
	VK_CHECK(getSemaphoreHandleFun(engine->_device, &semaphoreGetHandleInfo, &handle));

	return handle;
}

//TODO: Pretty sure I don't need handle type because I'm not making this platform agnostic...
//I also need to make sure I create the proper semaphores with export capabilities
void tsukiutil::getCudaSemaphore(TsukiEngine *engine, cudaExternalSemaphore_t *cudaSemaphore, VkSemaphore *vkSemaphore) {
	//https://github.com/NVIDIA/cuda-samples/blob/master/cpp/5_Domain_Specific/simpleVulkan/main.cpp : importCudaExternalSemaphore

	//cudaExternalSemaphoreHandleDesc is a struct telling CUDA about the external resource we're importing
	cudaExternalSemaphoreHandleDesc externalSemaphoreHandleDesc{};
	
	externalSemaphoreHandleDesc.type = cudaExternalSemaphoreHandleTypeOpaqueWin32;
	externalSemaphoreHandleDesc.handle.win32.handle = getVkSemaphoreHandle(engine, vkSemaphore);
	externalSemaphoreHandleDesc.flags = 0;

	CUDA_CHECK(cudaImportExternalSemaphore(cudaSemaphore, &externalSemaphoreHandleDesc));
}

HANDLE tsukiutil::getVkMemoryHandle(TsukiEngine *engine, VkDeviceMemory *memory) {
	//getMemHandle
	HANDLE handle = 0;

	VkMemoryGetWin32HandleInfoKHR memoryGetHandleInfo{};
	memoryGetHandleInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
	memoryGetHandleInfo.pNext = nullptr;
	memoryGetHandleInfo.memory = *memory;
	memoryGetHandleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

	PFN_vkGetMemoryWin32HandleKHR getMemoryWin32HandleKHRFun = (PFN_vkGetMemoryWin32HandleKHR)vkGetDeviceProcAddr(engine->_device, "vkGetMemoryWin32HandleKHR");
	if (!getMemoryWin32HandleKHRFun) {
		std::cerr << "Failed to retrieve vkGetMemoryWin32HandleKHR!" << std::endl;
		abort();
	}

	VK_CHECK(getMemoryWin32HandleKHRFun(engine->_device, &memoryGetHandleInfo, &handle));

	return handle;
}

//NOTE: cudaExternalMemory_t is a tracking object for GPU memory. So this function essentially allows us to register a portion of memory with CUDA
void tsukiutil::getCudaExternalMemory(TsukiEngine *engine, void **devicePointer, cudaExternalMemory_t *cudaMemory, VkDeviceMemory *vkMemory, VkDeviceSize memorySize, VkDeviceSize offset, VkDeviceSize bufferSize) {
	//Under importCudaExternalMemory

	//Import the entire VkDeviceMemory block (memorySize), then map only this buffer's
	//sub-range (offset .. offset + bufferSize). VMA suballocates many buffers into one block,
	//so offset is frequently non-zero; mapping at offset 0 would point CUDA at the wrong data.
	cudaExternalMemoryHandleDesc externalMemoryHandleDesc{};

	externalMemoryHandleDesc.type = cudaExternalMemoryHandleTypeOpaqueWin32;
	externalMemoryHandleDesc.size = memorySize;
	externalMemoryHandleDesc.handle.win32.handle = (HANDLE)getVkMemoryHandle(engine, vkMemory);
	CUDA_CHECK(cudaImportExternalMemory(cudaMemory, &externalMemoryHandleDesc));

	cudaExternalMemoryBufferDesc externalMemoryBufferDesc{};
	externalMemoryBufferDesc.offset = offset;
	externalMemoryBufferDesc.size = bufferSize;
	externalMemoryBufferDesc.flags = 0;
	CUDA_CHECK(cudaExternalMemoryGetMappedBuffer(devicePointer, *cudaMemory, &externalMemoryBufferDesc));
}
#pragma once
#include <cuda.h>
#include <cuda_runtime.h>
#include <iostream>

#include "vulkan/vulkan_core.h"

#define CUDA_CHECK(x)																																\
	do {																																			\
		cudaError_t err = x;																														\
		if (err != cudaSuccess) {																													\
			std::cerr << "CUDA error at file: " << __FILE__ << ", line: " << __LINE__ << ", error: " << cudaGetErrorString(err) << std::endl;	\
			abort();																																\
		}																																			\
	} while (0)

#define divup(a, b) ((a % b == 0) ? (a / b) : (a / b + 1))

#define BLOCKWIDTH 16
#define BLOCKHEIGHT 16

class TsukiEngine; //Forward declaration

struct TsukiLaunchDims {
	dim3 gridDim;
	dim3 blockDim;
};

struct u8vec4 {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
};

struct TsukiCudaData { //TODO: This bottlenecks performance. I might wanna improve this later...
	cudaExternalSemaphore_t _cudaSampleFinishedSemaphore;
	cudaExternalSemaphore_t _cudaCopyFinishedSemaphore;

	VkDeviceAddress imageBufferAddress;
	cudaExternalMemory_t imageBufferMemory;
	void *imageBuffer;
	int imageBufferSize{ 0 };

	VkDeviceAddress indexBufferAddress;
	cudaExternalMemory_t indexBufferMemory;
	void *indexBuffer;
	int indexBufferSize{ 0 };

	VkDeviceAddress VertexBufferAddress;
	cudaExternalMemory_t vertexBufferMemory;
	void *vertexBuffer;
	int vertexBufferSize{ 0 };

	bool resetImage{ false };

	unsigned numSamples;
};
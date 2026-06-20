#include "t_cudacommon.h"
#include "t_pathtrace.h"

__global__ void testImage(glm::vec4 *outImage, int width, int height) {
	int threadX = blockIdx.x * blockDim.x + threadIdx.x;
	int threadY = blockIdx.y * blockDim.y + threadIdx.y;

	if (threadX >= width || threadY >= height) { return; }

	glm::vec4 outColor{};

	outColor.x = (float)(threadX) / (float)(width);
	outColor.y = (float)(threadY) / (float)(height);

	outImage[threadY * width + threadX] = outColor;
}

//TSUKICUDAPATHTRACE
//===================================================================================================================
void tsukicudapathtrace::testImage(VkSemaphore waitSemaphore, VkSemaphore signalSemaphore, VkDeviceAddress outImage, int width, int height) {
	cudaExternalSemaphoreWaitParams waitParams{};
	memset(&waitParams, 0, sizeof(waitParams));
	waitParams.flags = 0;

	//TODO: Wait on external semaphore

	//TODO: Launch kernel

	//TODO: Signal external semaphore
}
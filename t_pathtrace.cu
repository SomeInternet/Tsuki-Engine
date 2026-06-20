#include "t_pathtrace.h"
#include "t_interop.h"

__global__ void kernTestImage(glm::vec4 *outImage, int width, int height) {
	int threadX = blockIdx.x * blockDim.x + threadIdx.x;
	int threadY = blockIdx.y * blockDim.y + threadIdx.y;

	if (threadX >= width || threadY >= height) { return; }

	glm::vec4 outColor{};

	outColor.r = (float)(threadX) / (float)(width);
	outColor.g = (float)(threadY) / (float)(height);
	outColor.b = 1;
	outColor.a = 1;

	outImage[threadY * width + threadX] = outColor;
}

//TSUKICUDAPATHTRACE
//===================================================================================================================
void tsukicudapathtrace::testImage(TsukiCudaData *cudaData, int width, int height) {
	//TODO: Wait on external semaphore
	cudaExternalSemaphoreWaitParams waitParams{}; //Unimportant for binary semaphores
	cudaWaitExternalSemaphoresAsync(&(cudaData->_cudaCopyFinishedSemaphore), &waitParams, 1);

	//TODO: Launch kernel
	TsukiLaunchDims dims;
	dims.gridDim = dim3(divup(width, BLOCKWIDTH), divup(height, BLOCKHEIGHT), 1);
	dims.blockDim = dim3(BLOCKWIDTH, BLOCKHEIGHT, 1);
	kernTestImage << <dims.gridDim, dims.blockDim >> > (reinterpret_cast<glm::vec4 *>(cudaData->imageBufferAddress), width, height);

	//TODO: Signal external semaphore
	cudaExternalSemaphoreSignalParams signalParams{};
	cudaSignalExternalSemaphoresAsync(&(cudaData->_cudaSampleFinishedSemaphore), &signalParams, 1);
}
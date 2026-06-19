#include <cuda_runtime.h>
#include <iostream>

#define CUDA_CHECK(x)																																\
	do {																																			\
		cudaError_t err = x;																														\
		if (err != cudaSuccess) {																													\
			std::cerr << "CUDA error at file: " << __FILE__ << ", line: " << __LINE__ << ", error: " << cudaGetErrorString(err), err) << std::endl;	\
			abort();																																\
		}																																			\
	} while (0)

class TsukiEngine; //Forward declaration

struct cudaFrameData {

};

struct TsukiCudaData { //TODO: This bottlenecks performance. I might wanna improve this later...
	cudaExternalSemaphore_t _cudaSampleFinishedSemaphores;
	cudaExternalSemaphore_t _cudaCopyFinishedSemaphore;

	cudaExternalMemory_t indexBuffer;
	cudaExternalMemory_t vertexBuffer;
	cudaExternalMemory_t accumulatorImage;

	bool resetImage{ false };

	unsigned numSamples;
};
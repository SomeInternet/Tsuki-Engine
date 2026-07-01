#pragma once
#include <cuda.h>
#include <cuda_runtime.h>
#include <iostream>

#include "vulkan/vulkan_core.h"

#define GLM_FORCE_CUDA 
#define GLM_FORCE_RADIANS 
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "t_geometry.h"

#include <curand.h>
#include <curand_kernel.h>

#define CUDA_CHECK(x)																																\
	do {																																			\
		cudaError_t err = x;																														\
		if (err != cudaSuccess) {																													\
			std::cerr << "CUDA error at file: " << __FILE__ << ", line: " << __LINE__ << ", error: " << cudaGetErrorString(err) << std::endl;	\
			abort();																																\
		}																																			\
	} while (0)

#define PI 3.14159265358979323846264338327950288f

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

class TsukiCamera;
struct TsukiCudaMesh;

struct TsukiCudaCamera {
	glm::vec3 origin;
	glm::vec3 forward;
	glm::vec3 right;
	glm::vec3 up;
	float fov;
};

struct TsukiCudaMesh {
	int numIndices{ 0 };
	cudaExternalMemory_t indexBufferMemory;
	uint32_t *d_indexBuffer;

	int numVertices{ 0 };
	cudaExternalMemory_t posMemory;
	glm::vec4 *d_pos;

	cudaExternalMemory_t normalMemory;
	glm::vec4 *d_normal;

	cudaExternalMemory_t colorMemory;
	glm::vec4 *d_color;

	cudaExternalMemory_t uvMemory;
	glm::vec2 *d_uv;

	int materialLookupBufferSize{ 0 };
	cudaExternalMemory_t MaterialLookupBufferMemory;
	int *d_materialLookupBuffer;
};

enum class TsukiMaterialPass : uint8_t {
	TSUKI_MATERIAL_OPAQUE, TSUKI_MATERIAL_TRANSPARENT, TSUKI_MATERIAL_OTHER
};

struct TsukiMaterialData {
	int colorTextureIndex{ -1 };
	int metallicRoughnessTextureIndex{ -1 };
	int emissiveTextureIndex{ -1 };
	int normalTextureIndex{ -1 };

	glm::vec4 colorFac;
	glm::vec4 metallicRoughnessFac;
	glm::vec4 emissionFac;

	TsukiMaterialPass pass;
	uint8_t passPadding[15];

	glm::vec4 padding[11]; //Padding to meet the 256 bytes
};

struct TsukiCudaAccelerationStructures {
	int tlasSize{ 0 };
	TLASNode *d_tlas;

	int numBVH{ 0 };
	glm::mat4 *d_transforms;
	glm::mat4 *d_invTransforms;
	int *d_bvhSizes;
	BVHNode **d_bvh;
};

struct TsukiCudaData { //TODO: This bottlenecks performance. I might wanna improve this later...
	cudaExternalSemaphore_t _cudaSampleFinishedSemaphore;
	cudaExternalSemaphore_t _cudaCopyFinishedSemaphore;

	cudaExternalSemaphore_t _cudaASBuildFinishedSemaphore;

	cudaExternalMemory_t imageBufferMemory;
	void *imageBuffer{ nullptr };
	int imageBufferSize{ 0 };

	bool asBuilt{ false };
	bool asDirty{ false };

	unsigned numSamples{ 0 };

	TsukiCudaCamera *d_camera{ nullptr };

	//The material buffer is exported to CUDA from Vulkan
	//Those who forget it has to be loaded to Vulkan for the rasterized view...
	cudaExternalMemory_t materialBufferMemory;
	TsukiMaterialData *d_materialBuffer{ nullptr };

	int numMeshes;
	TsukiCudaMesh *d_meshes{ nullptr };
	TsukiCudaAccelerationStructures *d_accelerationStructures{ nullptr };

	curandState *d_curandStates{ nullptr };
};
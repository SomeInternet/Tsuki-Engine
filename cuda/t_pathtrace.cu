#define GLM_FORCE_CUDA 
#include "t_pathtrace.h"
#include "t_interop.h"
#include "t_intersect.h"

//TODO: Investigate slight precision issues at edge boundaries
__device__ Intersection intersectScene(TsukiCudaAccelerationStructures *accelerationStructures, TsukiCudaMesh *meshes, TsukiMaterialData *materials,
	TsukiCudaCamera *camera, Ray ray) {
	//Traverse the acceleration structures (stackless skip-pointer traversal).
	Intersection result{};
	float minT = INFINITY;

	int instanceId = -1;
	int currTLASNodeId = 0;
	//Outer loop: Traverse TLAS
	do {
		TLASNode currTLASNode = accelerationStructures->d_tlas[currTLASNodeId];
		bool tlasHit = tsukiintersect::intersectBoundingBox(currTLASNode.bounds, ray) < minT;

		if (!tlasHit) {
			currTLASNodeId = currTLASNode.nextNodeId; //Miss
		}
		else if (currTLASNode.blasNodeId >= 0) { //Hit, and leaf
			int currMeshId = currTLASNode.blasNodeId;
			BVHNode *bvh = accelerationStructures->d_bvh[currMeshId];
			int bvhSize = accelerationStructures->d_bvhSizes[currMeshId];

			int currBLASNodeId = 0;
			//Inner loop: traverse BLAS
			//Warp ray with the transform of the node
			Ray warpedRay{};
			warpedRay.origin = glm::vec3(accelerationStructures->d_invTransforms[currTLASNode.blasNodeId] * glm::vec4(ray.origin, 1));
			warpedRay.dir = glm::vec3(accelerationStructures->d_invTransforms[currTLASNode.blasNodeId] * glm::vec4(ray.dir, 0));
			//warpedRay.origin = ray.origin;
			//warpedRay.dir = ray.dir;
			do {
				BVHNode currBLASNode = bvh[currBLASNodeId];
				bool blasHit = tsukiintersect::intersectBoundingBox(currBLASNode.bounds, warpedRay) < minT;

				if (!blasHit) {
					currBLASNodeId = currBLASNode.nextNodeId; //Miss
				}
				else if (currBLASNode.primitiveId >= 0) { //Hit and leaf
					uint32_t i1 = meshes[currMeshId].d_indexBuffer[currBLASNode.primitiveId * 3];
					uint32_t i2 = meshes[currMeshId].d_indexBuffer[currBLASNode.primitiveId * 3 + 1];
					uint32_t i3 = meshes[currMeshId].d_indexBuffer[currBLASNode.primitiveId * 3 + 2];

					glm::vec3 v1 = glm::vec3(meshes[currMeshId].d_pos[i1]);
					glm::vec3 v2 = glm::vec3(meshes[currMeshId].d_pos[i2]);
					glm::vec3 v3 = glm::vec3(meshes[currMeshId].d_pos[i3]);

					TriangleIntersection triIsect = tsukiintersect::intersectTriangle(v1, v2, v3, warpedRay);
					if (triIsect.t > EPSILON && triIsect.t < result.t) { 
						result.t = triIsect.t;
						result.meshId = currMeshId;
						result.primitiveId = currBLASNode.primitiveId;
						result.u = triIsect.u;
						result.v = triIsect.v;

						instanceId = currTLASNode.blasNodeId; //Store the instance number to get the inverse transpose to apply to the normal
					}

					currBLASNodeId = currBLASNode.nextNodeId; //Tree done, escape
				}
				else { //Hit and interior node, so traverse deeper (BLAS)
					++currBLASNodeId;
				}
			} while (currBLASNodeId != 0 && currBLASNodeId < bvhSize);

			currTLASNodeId = currTLASNode.nextNodeId; //Tree done, escape
		}
		else { //Hit and interior node, so traverse deeper (TLAS)
			++currTLASNodeId;
		}
	} while (currTLASNodeId != 0 && currTLASNodeId < accelerationStructures->tlasSize);

	if (result.primitiveId >= 0) {
		glm::vec3 v1Nor = meshes[result.meshId].d_normal[meshes[result.meshId].d_indexBuffer[3 * result.primitiveId]];
		glm::vec3 v2Nor = meshes[result.meshId].d_normal[meshes[result.meshId].d_indexBuffer[3 * result.primitiveId + 1]];
		glm::vec3 v3Nor = meshes[result.meshId].d_normal[meshes[result.meshId].d_indexBuffer[3 * result.primitiveId + 2]];
		if (v1Nor == v2Nor && v2Nor == v3Nor) { result.normal = glm::vec3(v1Nor); }
		else { result.normal = glm::vec3((1.f - result.u - result.v) * v1Nor + result.u * v2Nor + result.v * v3Nor); }

		//Transform the normal by the object transform
		result.normal = glm::transpose(glm::mat3(accelerationStructures->d_invTransforms[instanceId])) * result.normal;
	}
	return result;
}

__global__ void kernTestImage(glm::vec4 *outImage, int width, int height) {
	int threadX = blockIdx.x * blockDim.x + threadIdx.x;
	int threadY = blockIdx.y * blockDim.y + threadIdx.y;

	if (threadX >= width || threadY >= height) { return; }

	glm::vec4 outColor{};

	outColor.r = (float)(threadX) / (float)(width);
	outColor.g = (float)(threadY) / (float)(height);
	outColor.b = 0.f;
	outColor.a = 1;

	outImage[threadY * width + threadX] = outColor;
}

__global__ void kernTestRaytrace(glm::vec4 *outImage, TsukiCudaAccelerationStructures *accelerationStructures, TsukiCudaMesh *meshes, TsukiMaterialData *materials, 
	TsukiCudaCamera *camera, int width, int height) {
	int threadX = blockIdx.x * blockDim.x + threadIdx.x;
	int threadY = blockIdx.y * blockDim.y + threadIdx.y;

	if (threadX >= width || threadY >= height) { return; }

	float screenX = (float)(threadX - width / 2) / (float)(width / 2);
	float screenY = (float)(height / 2 - threadY) / (float)(height / 2);

	glm::vec3 ref = camera->origin + camera->forward;
	float tanFov = glm::tan(glm::radians(camera->fov / 2.f));
	glm::vec3 v = tanFov * camera->up;
	glm::vec3 h = tanFov * (float)(width) / (float)(height) * camera->right;
	glm::vec3 screenPoint = ref + screenX * h + screenY * v;

	//Create the ray
	Ray ray{};
	ray.origin = camera->origin;
	ray.dir = glm::normalize(screenPoint - ray.origin);

	if (accelerationStructures->tlasSize <= 0) {
		outImage[threadY * width + threadX] = glm::vec4(0);
		return;
	}

	Intersection isect = intersectScene(accelerationStructures, meshes, materials, camera, ray);

	if (isect.t < INFINITY) {
		//Lambertian lighting
		//Recall that the ray direction has to be inverted because it's oriented into the face, and the normal is oriented out of the face
		outImage[threadY * width + threadX] = glm::vec4(glm::abs(glm::vec3(glm::dot(isect.normal, -ray.dir))), 1);
	}
	else {
		outImage[threadY * width + threadX] = glm::vec4(ray.dir, 1);
	}
}

__global__ void kernTestPathtrace(glm::vec4 *outImage, TsukiCudaAccelerationStructures *accelerationStructures, TsukiCudaMesh *meshes, TsukiMaterialData *materials, 
	TsukiCudaCamera *camera, int width, int height, unsigned numSamples) {

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
	kernTestImage <<<dims.gridDim, dims.blockDim>>> (reinterpret_cast<glm::vec4 *>(cudaData->imageBuffer), width, height);

	//TODO: Signal external semaphore
	cudaExternalSemaphoreSignalParams signalParams{};
	cudaSignalExternalSemaphoresAsync(&(cudaData->_cudaSampleFinishedSemaphore), &signalParams, 1);
}

void tsukicudapathtrace::testRaytrace(TsukiCudaData *cudaData, int width, int height) {
	//TODO: Wait on external semaphore
	cudaExternalSemaphoreWaitParams waitParams{}; //Unimportant for binary semaphores
	cudaWaitExternalSemaphoresAsync(&(cudaData->_cudaCopyFinishedSemaphore), &waitParams, 1);

	//TODO: Launch kernel
	TsukiLaunchDims dims;
	dims.gridDim = dim3(divup(width, BLOCKWIDTH), divup(height, BLOCKHEIGHT), 1);
	dims.blockDim = dim3(BLOCKWIDTH, BLOCKHEIGHT, 1);
	kernTestRaytrace << <dims.gridDim, dims.blockDim >> > (reinterpret_cast<glm::vec4 *>(cudaData->imageBuffer), cudaData->d_accelerationStructures, cudaData->d_meshes,
		cudaData->d_materialBuffer, cudaData->d_camera, width, height);

	CUDA_CHECK(cudaGetLastError());
	CUDA_CHECK(cudaDeviceSynchronize());

	//TODO: Signal external semaphore
	cudaExternalSemaphoreSignalParams signalParams{};
	cudaSignalExternalSemaphoresAsync(&(cudaData->_cudaSampleFinishedSemaphore), &signalParams, 1);
}

void tsukicudapathtrace::testPathtrace(TsukiCudaData *cudaData, int width, int height) {
	//TODO: Initialize cuRand (not here)

	//TODO: Launch pathtracing kernels

	//TODO: Signal semaphores
}
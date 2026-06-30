#define GLM_FORCE_CUDA 
#include "t_pathtrace.h"
#include "t_interop.h"
#include "t_intersect.h"

__host__ __device__ inline float tsukiintersect::intersectBoundingBox(Bounds bounds, Ray ray) {
	//Ray-box intersection via the slab method.
	//tMin = max of the per-axis entry distances, tMax = min of the per-axis exit distances.
	float tMin = -INFINITY;
	float tMax = INFINITY;

	for (int i = 0; i < 3; ++i) {
		float invD = 1.f / ray.dir[i];
		float t0 = (bounds.min[i] - ray.origin[i]) * invD;
		float t1 = (bounds.max[i] - ray.origin[i]) * invD;

		//Swap so t0 is the entry and t1 the exit when the ray points down this axis
		if (invD < 0.f) {
			float tmp = t0; t0 = t1; t1 = tmp;
		}

		tMin = t0 > tMin ? t0 : tMin;
		tMax = t1 < tMax ? t1 : tMax;
	}

	//Miss if the slabs don't overlap, or the box is entirely behind the ray
	if (tMax < tMin || tMax < 0.f) { return INFINITY; }

	//Nearest entry distance (0 if the ray origin is inside the box)
	return (tMin > 0.f) ? tMin : 0.f;
}

__host__ __device__ inline float tsukiintersect::intersectTriangle(glm::vec3 v1, glm::vec3 v2, glm::vec3 v3, Ray ray) {
	//Ray-triangle intersection via the M�ller-Trumbore algorithm

	glm::vec3 e1 = v2 - v1;
	glm::vec3 e2 = v3 - v1;
	glm::vec3 normal = glm::cross(e1, e2);

	//Check if ray is parallel to the plane
	glm::vec3 crossRayE2 = glm::cross(ray.dir, e2); //pvec
	float determinant = glm::dot(crossRayE2, e1);
	if (fabsf(determinant) < EPSILON) { return INFINITY; }

	float invDeterminant = 1.f / determinant;

	glm::vec3 s = ray.origin - v1; //tvec
	float u = invDeterminant * glm::dot(s, crossRayE2); //u = dot(tvec, pvec) / det

	if (u < -EPSILON || u - 1.f > EPSILON) { return INFINITY; }

	glm::vec3 crossSE1 = glm::cross(s, e1); //qvec
	float v = invDeterminant * glm::dot(ray.dir, crossSE1); //v = dot(dir, qvec) / det

	if (v < -EPSILON || u + v - 1.f > EPSILON) { return INFINITY; }

	return invDeterminant * glm::dot(e2, crossSE1); //t = dot(e2, qvec) / det
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

__global__ void kernTestRaytrace(glm::vec4 *outImage, TsukiCudaAccelerationStructures *accelerationStructures, TsukiCudaMesh *meshes, glm::mat4 *transforms, 
	glm::mat4 *invTransforms, TsukiCudaCamera *camera, int width, int height) {
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

	//Traverse the acceleration structures (stackless skip-pointer traversal).
	//At each node: miss -> jump to escape (nextNodeId); hit leaf -> process then escape;
	//hit interior -> descend to the left child (id + 1).
	float minT = INFINITY;

	//Nothing to trace against yet (TLAS not built / empty scene)
	if (accelerationStructures->tlasSize <= 0) {
		outImage[threadY * width + threadX] = glm::vec4(0);
		return;
	}

	int currTLASNodeId = 0;
	do {
		TLASNode currTLASNode = accelerationStructures->d_tlas[currTLASNodeId];
		bool tlasHit = tsukiintersect::intersectBoundingBox(currTLASNode.bounds, ray) < minT;

		if (!tlasHit) {
			currTLASNodeId = currTLASNode.nextNodeId; //Miss: skip this subtree
		}
		else if (currTLASNode.blasNodeId >= 0) { //TLAS leaf: traverse this instance's BLAS
			int meshId = currTLASNode.blasNodeId;
			BVHNode *bvh = accelerationStructures->d_bvh[meshId];
			int bvhSize = accelerationStructures->d_bvhSizes[meshId];

			int currBLASNodeId = 0;
			do {
				BVHNode currBLASNode = bvh[currBLASNodeId];
				bool blasHit = tsukiintersect::intersectBoundingBox(currBLASNode.bounds, ray) < minT;

				if (!blasHit) {
					currBLASNodeId = currBLASNode.nextNodeId; //Miss: skip subtree
				}
				else if (currBLASNode.primitiveId >= 0) { //BLAS leaf: triangle test
					uint32_t i1 = meshes[meshId].d_indexBuffer[currBLASNode.primitiveId * 3];
					uint32_t i2 = meshes[meshId].d_indexBuffer[currBLASNode.primitiveId * 3 + 1];
					uint32_t i3 = meshes[meshId].d_indexBuffer[currBLASNode.primitiveId * 3 + 2];

					glm::vec3 v1 = glm::vec3(meshes[meshId].d_pos[i1]);
					glm::vec3 v2 = glm::vec3(meshes[meshId].d_pos[i2]);
					glm::vec3 v3 = glm::vec3(meshes[meshId].d_pos[i3]);

					float triT = tsukiintersect::intersectTriangle(v1, v2, v3, ray);
					if (triT > EPSILON && triT < minT) { minT = triT; }

					currBLASNodeId = currBLASNode.nextNodeId; //Leaf: escape
				}
				else { //Interior hit: descend to left child
					++currBLASNodeId;
				}
			} while (currBLASNodeId != 0 && currBLASNodeId < bvhSize);

			currTLASNodeId = currTLASNode.nextNodeId; //Leaf done: escape
		}
		else { //TLAS interior hit: descend to left child
			++currTLASNodeId;
		}
	} while (currTLASNodeId != 0 && currTLASNodeId < accelerationStructures->tlasSize);

	if (minT < INFINITY) {
		outImage[threadY * width + threadX] = glm::vec4(1);
	}
	else {
		outImage[threadY * width + threadX] = glm::vec4(ray.dir, 1);
	}
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
	kernTestRaytrace <<<dims.gridDim, dims.blockDim >>> (reinterpret_cast<glm::vec4 *>(cudaData->imageBuffer), cudaData->d_accelerationStructures, cudaData->d_meshes,
		nullptr, nullptr, cudaData->d_camera, width, height);

	CUDA_CHECK(cudaGetLastError());
	CUDA_CHECK(cudaDeviceSynchronize());

	//TODO: Signal external semaphore
	cudaExternalSemaphoreSignalParams signalParams{};
	cudaSignalExternalSemaphoresAsync(&(cudaData->_cudaSampleFinishedSemaphore), &signalParams, 1);
}
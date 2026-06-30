#pragma once
#define GLM_FORCE_CUDA
#include <glm/glm.hpp>
#include <vector>

enum class Axis : uint8_t {
	AXIS_X, AXIS_Y, AXIS_Z
};

enum class BVHHeuristic : uint8_t {
	EQUAL_PRIMITIVE, SURFACE_AREA
};

struct Bounds {
	glm::vec3 min;
	glm::vec3 max;

	float surfaceArea() {
		glm::vec3 extent = max - min;
		return 2.f * ((extent.x * extent.y) + (extent.y * extent.z) + (extent.z * extent.x));
	}
};

//BLAS CLASSES
//===================================================================================================================
struct BVHTriangleInfo {
	int primitiveId;
	Bounds bounds;
	glm::vec3 centroid;
};

struct SAHBucket {
	int count{ 0 };
	Bounds bounds;
};

struct BuildBVHNode {
	Bounds bounds;

	//If it's negative, then not primitive
	int primitiveId; //multiply by 3 to get the starting index into the index buffer

	//0 denotes no child
	uint32_t leftChildId{ 0 };
	uint32_t rightChildId{ 0 };

	//For stackless BVH traversal
	uint32_t nextNodeId{ 0 };
};

struct BVHNode {
	//Because the tree is built depth first, the left child should always be the next node.
	//I can check the sign of primitiveId to know if it's a leaf or not.
	//This should fit in 32 bits...
	Bounds bounds;
	int primitiveId{ -1 };
	uint32_t nextNodeId;
};

//TLAS CLASSES
//===================================================================================================================
struct BLASInstance {
	glm::mat4 transform{ 1 };
	int blasNodeId;
};

struct BLASInfo {
	Bounds bounds;
	int blasInstanceId;
	uint32_t blasId;
};

struct BuildTLASNode {
	int blasNodeId;
	Bounds bounds;
	uint32_t leftChildId{ 0 };
	uint32_t rightChildId{ 0 };
	uint32_t nextNodeId{ 0 };
};

struct TLASNode { //Follows similar logic to the BVHNode
	Bounds bounds;
	int blasNodeId{ -1 };
	uint32_t nextNodeId;
};
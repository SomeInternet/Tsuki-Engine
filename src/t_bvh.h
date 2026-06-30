#pragma once
#include "t_geometry.h"

namespace tsukibvh {
	//BLAS FUNCTIONS
	//===================================================================================================================
	std::vector<BVHTriangleInfo> parseTriangles(std::vector<uint32_t> &indices, std::vector<glm::vec4> &pos);

	std::vector<BVHNode> buildBVH(std::vector<uint32_t> &indices, std::vector<glm::vec4> &pos, BVHHeuristic heuristic);

	uint32_t recursiveBuildBVH(std::vector<BuildBVHNode> &result, std::vector<BVHTriangleInfo> &triangles, uint32_t parent, int start, int end, BVHHeuristic heuristic);

	void setNextPointers(std::vector<BuildBVHNode> &bvh, uint32_t curr, uint32_t escape);

	//TLAS FUNCTIONS
	//===================================================================================================================
	std::vector<BLASInfo> parseAABBs(std::vector <BVHNode> &blas, std::vector<BLASInstance> &instances, std::vector<glm::mat4> &transforms);

	std::vector<TLASNode> buildTLAS(std::vector<BVHNode> &blas, std::vector<BLASInstance> &instances, std::vector<glm::mat4> &transforms);

	uint32_t recursiveBuildTLAS(std::vector<BuildTLASNode> &result, std::vector<BLASInfo> &blas, uint32_t parent, int start, int end);

	void setNextPointers(std::vector<BuildTLASNode> &tlas, uint32_t curr, uint32_t escape);
};

//HELPERS
//===================================================================================================================
namespace tsukiutil {
	Bounds unionBounds(Bounds a, Bounds b);
};
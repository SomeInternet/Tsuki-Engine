#include "t_bvh.h"
#include <limits>
#include <algorithm>

constexpr int numBuckets = 12;

Bounds tsukiutil::unionBounds(Bounds a, Bounds b) {
	Bounds result;
	result.min = glm::min(a.min, b.min);
	result.max = glm::max(a.max, b.max);
	return result;
}

//BLAS FUNCTIONS
//===================================================================================================================
std::vector<BVHTriangleInfo> tsukibvh::parseTriangles(std::vector<uint32_t> &indices, std::vector<glm::vec4> &pos) {
	std::vector<BVHTriangleInfo> result;
	for (int i = 0; i < indices.size() / 3; ++i) {
		BVHTriangleInfo triangle;
		triangle.primitiveId = i;

		glm::vec3 v1 = glm::vec3(pos[indices[3 * i]]);
		glm::vec3 v2 = glm::vec3(pos[indices[3 * i + 1]]);
		glm::vec3 v3 = glm::vec3(pos[indices[3 * i + 2]]);

		triangle.centroid = (v1 + v2 + v3) / 3.f;
		triangle.bounds.min = glm::min(glm::min(v1, v2), v3);
		triangle.bounds.max = glm::max(glm::max(v1, v2), v3);

		result.push_back(triangle);
	}

	return result;
}

std::vector<BVHNode> tsukibvh::buildBVH(std::vector<uint32_t> &indices, std::vector<glm::vec4> &pos, BVHHeuristic heuristic) {
	//Builds a BVH using the specified heuristic
	std::vector<BVHTriangleInfo> triangles = tsukibvh::parseTriangles(indices, pos);

	//Call the recursive BVH build helper with the heuristic
	std::vector<BuildBVHNode> buildBVHNodes;
	
	recursiveBuildBVH(buildBVHNodes, triangles, 0, 0, triangles.size(), (triangles.size() <= 12) ? BVHHeuristic::EQUAL_PRIMITIVE : heuristic);

	setNextPointers(buildBVHNodes, 0, 0);

	//Compress the BVH nodes
	std::vector<BVHNode> result;
	for (auto &node : buildBVHNodes) {
		BVHNode newNode{};
		newNode.bounds = node.bounds;
		newNode.primitiveId = node.primitiveId;
		newNode.nextNodeId = node.nextNodeId;
		result.push_back(newNode);
	}

	return result;
}

uint32_t tsukibvh::recursiveBuildBVH(std::vector<BuildBVHNode> &result, std::vector<BVHTriangleInfo> &triangles, uint32_t parent, int start, int end, BVHHeuristic heuristic) {
	if (end - start <= 1) { //Leaf node case
		BuildBVHNode newNode{};
		newNode.primitiveId = triangles[start].primitiveId;
		int id = result.size();
		result.push_back(newNode);
		return id;
	}
	
	Bounds currBounds = triangles[start].bounds;

	//Compute bounds for this node
	for (int i = start; i < end; ++i) {
		currBounds = tsukiutil::unionBounds(currBounds, triangles[i].bounds);
	}

	glm::vec3 extent = glm::vec3(currBounds.max.x - currBounds.min.x, 
		currBounds.max.y - currBounds.min.y, 
		currBounds.max.z - currBounds.min.z);
	float longestAxis = std::max(std::max(extent.x, extent.y), extent.z);
	int splitAxis = static_cast<int>((longestAxis == extent.x) ? Axis::AXIS_X : (longestAxis == extent.y) ? Axis::AXIS_Y : Axis::AXIS_Z);

	int mid;

	if (heuristic == BVHHeuristic::EQUAL_PRIMITIVE) { //Partition all elements from start to end around the middle triangle with respect to the split axis
		mid = (start + end) / 2;
		std::nth_element(triangles.begin() + start, triangles.begin() + mid,
			triangles.begin() + end,
			[splitAxis](const BVHTriangleInfo &a, const BVHTriangleInfo &b) {
				return a.centroid[splitAxis] < b.centroid[splitAxis];
			});
	}
	else if (heuristic == BVHHeuristic::SURFACE_AREA) { //Place all triangles into the buckets, and update the bounds of the buckets
		SAHBucket buckets[numBuckets];
		for (int i = 0; i < numBuckets; ++i) {
			buckets[i].bounds.min = glm::vec3(std::numeric_limits<float>::max());
			buckets[i].bounds.max = glm::vec3(-std::numeric_limits<float>::max());
		}

		for (int i = start; i < end; ++i) {
			int bucket = static_cast<int>(static_cast<float>(numBuckets) * (triangles[i].centroid[splitAxis] - currBounds.min[splitAxis]) / (currBounds.max[splitAxis] - currBounds.min[splitAxis]));
			bucket = (bucket < numBuckets) ? bucket : numBuckets - 1;

			++buckets[bucket].count;
			buckets[bucket].bounds = tsukiutil::unionBounds(buckets[bucket].bounds, triangles[i].bounds);
		}

		//Compute the cost of splitting at each nontrivial boundary
		float minCost = std::numeric_limits<float>::max();
		int splitBucket = 0;
		for (int i = 0; i < numBuckets - 1; ++i) {
			Bounds b0, b1;
			int count0{ 0 };
			int count1{ 0 };

			for (int j = 0; j <= i; ++j) {
				b0 = tsukiutil::unionBounds(b0, buckets[j].bounds);
				count0 += buckets[j].count;
			}

			for (int j = i + 1; j < numBuckets; ++j) {
				b1 = tsukiutil::unionBounds(b1, buckets[j].bounds);
				count1 += buckets[j].count;
			}

			float currCost = .125f * (static_cast<float>(count0) * b0.surfaceArea() + static_cast<float>(count1) * b1.surfaceArea()) / currBounds.surfaceArea();
			if (currCost < minCost) {
				splitBucket = i;
				minCost = currCost;
			}
		}

		auto midIterator = std::partition(triangles.begin() + start, triangles.begin() + end,
			[=](const BVHTriangleInfo &tri) {
				int b = static_cast<int>(static_cast<float>(numBuckets) * (tri.centroid[splitAxis] - currBounds.min[splitAxis]) / std::max(currBounds.max[splitAxis] - currBounds.min[splitAxis], .0001f));
				b = (b < numBuckets) ? b : numBuckets - 1;
				return b <= splitBucket;
			});
		mid = std::distance(triangles.begin(), midIterator);
	}

	//TODO: Update the BVHNodes vector...
	BuildBVHNode newNode{};
	newNode.bounds = currBounds;
	newNode.primitiveId = -1;
	newNode.leftChildId = 0;
	newNode.rightChildId = 0;

	int id = result.size();
	result.push_back(newNode);

	int leftChildId = recursiveBuildBVH(result, triangles, id, start, mid, (mid - start <= 12) ? BVHHeuristic::EQUAL_PRIMITIVE : heuristic);
	int rightChildId = recursiveBuildBVH(result, triangles, id, mid, end, (end - mid <= 12) ? BVHHeuristic::EQUAL_PRIMITIVE : heuristic);

	result[id].leftChildId = leftChildId;
	result[id].rightChildId = rightChildId;
	return id;
}

void tsukibvh::setNextPointers(std::vector<BuildBVHNode> &bvh, uint32_t curr, uint32_t escape) {
	BuildBVHNode &currNode = bvh[curr];
	currNode.nextNodeId = escape;

	if (currNode.leftChildId > 0 && currNode.rightChildId > 0) {
		//Right child escapes to whatever this node escapes to
		setNextPointers(bvh, currNode.rightChildId, escape);

		//Left child escapes to the right child
		setNextPointers(bvh, currNode.leftChildId, currNode.rightChildId);
	}
}

//TLAS FUNCTIONS
//===================================================================================================================
std::vector<BLASInfo> tsukibvh::parseAABBs(std::vector <BVHNode> &blas, std::vector<BLASInstance> &instances, std::vector<glm::mat4> &transforms) {
	std::vector<BLASInfo> result;

	for (int i = 0; i < instances.size(); ++i) {
		auto &node = blas[instances[i].blasNodeId];
		auto &transform = transforms[i];

		glm::vec3 extent = node.bounds.max - node.bounds.min;
		glm::vec3 corners[8];

		for (int j = 0; j < 8; ++j) {
			corners[j] = node.bounds.min;
			if (j % 2 == 1) {
				corners[j] += glm::vec3(0, 0, extent.z);
			}
			if (j % 4 >= 2) {
				corners[j] += glm::vec3(0, extent.y, 0);
			}
			if (j % 8 >= 4) {
				corners[j] += glm::vec3(extent.x, 0, 0);
			}
		}
		

		Bounds bounds;
		for (int j = 0; j < 8; ++j) {
			corners[j] = glm::vec3(transform * glm::vec4(corners[j], 1));
			bounds.min = (j == 0) ? corners[0] : glm::min(bounds.min, corners[j]);
			bounds.max = (j == 0) ? corners[0] : glm::max(bounds.max, corners[j]);
		}
		BLASInfo blas{};
		blas.bounds = bounds;
		blas.blasInstanceId = i;
		result.push_back(blas);
	}
	return result;
}

std::vector<TLASNode> tsukibvh::buildTLAS(std::vector<BVHNode> &blas, std::vector<BLASInstance> &instances, std::vector<glm::mat4> &transforms) {
	std::vector<BLASInfo> aabbs = tsukibvh::parseAABBs(blas, instances, transforms);

	//TODO: Use the aabbs to build the TLAS
	std::vector<BuildTLASNode> buildNodes;
	recursiveBuildTLAS(buildNodes, aabbs, 0, 0, aabbs.size());

	//TODO: Set the next pointers for stackless traversal
	setNextPointers(buildNodes, 0, 0);

	//TODO: Convert the TLAS build nodes to TLAS nodes
	std::vector<TLASNode> result;
	for (auto &buildNode : buildNodes) {
		TLASNode newNode{};
		newNode.blasNodeId = buildNode.blasNodeId;
		newNode.bounds = buildNode.bounds;
		newNode.nextNodeId = buildNode.nextNodeId;
		result.push_back(newNode);
	}

	return result;
}

uint32_t tsukibvh::recursiveBuildTLAS(std::vector<BuildTLASNode> &result, std::vector<BLASInfo> &aabbs, uint32_t parent, int start, int end) {
	if (end - start == 1) {
		BuildTLASNode newNode{};
		newNode.blasNodeId = aabbs[start].blasInstanceId;
		newNode.bounds = aabbs[start].bounds;
		int id = result.size();
		result.push_back(newNode);
		return id;
	}

	Bounds currBounds = aabbs[start].bounds;

	for (int i = start; i < end; ++i) {
		currBounds = tsukiutil::unionBounds(currBounds, aabbs[i].bounds);
	}

	glm::vec3 extent = glm::vec3(currBounds.max.x - currBounds.min.x,
		currBounds.max.y - currBounds.min.y,
		currBounds.max.z - currBounds.min.z);
	float longestAxis = std::max(std::max(extent.x, extent.y), extent.z);
	int splitAxis = static_cast<int>((longestAxis == extent.x) ? Axis::AXIS_X : (longestAxis == extent.y) ? Axis::AXIS_Y : Axis::AXIS_Z);

	int mid = (start + end) / 2;
	std::nth_element(aabbs.begin() + start, aabbs.begin() + mid,
		aabbs.begin() + end,
		[splitAxis](const BLASInfo &a, const BLASInfo &b) {
			glm::vec3 centroidA = (a.bounds.min + a.bounds.max) / 2.f;
			glm::vec3 centroidB = (b.bounds.min + b.bounds.max) / 2.f;
			return centroidA[splitAxis] < centroidB[splitAxis];
		});
	
	uint32_t id = result.size();
	BuildTLASNode newNode{};
	newNode.blasNodeId = -1;
	newNode.bounds = currBounds;
	newNode.leftChildId = 0;
	newNode.rightChildId = 0;
	result.push_back(newNode);

	uint32_t leftChildId = recursiveBuildTLAS(result, aabbs, id, start, mid);
	uint32_t rightChildId = recursiveBuildTLAS(result, aabbs, id, mid, end);

	result[id].leftChildId = leftChildId;
	result[id].rightChildId = rightChildId;

	return id;
}

void tsukibvh::setNextPointers(std::vector<BuildTLASNode> &tlas, uint32_t curr, uint32_t escape) {
	auto &currNode = tlas[curr];
	currNode.nextNodeId = escape;

	if (currNode.leftChildId > 0 && currNode.rightChildId > 0) {
		//Right child escapes to whatever this node escapes to
		setNextPointers(tlas, currNode.rightChildId, escape);

		//Left child escapes to the right child
		setNextPointers(tlas, currNode.leftChildId, currNode.rightChildId);
	}
}
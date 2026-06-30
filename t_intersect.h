#pragma once
#include "t_cudacommon.h"
#include "t_geometry.h"

#define INFINITY 0x1.fffffep+127f
#define EPSILON 1.1920928955078125e-7f

struct Ray {
	glm::vec3 origin;
	glm::vec3 dir;
};

struct Intersection {
	int primitiveId{ -1 };
	float t{ INFINITY };
};

namespace tsukiintersect {
	__host__ __device__ inline float intersectBoundingBox(Bounds bounds, Ray ray);

	__host__ __device__ inline float intersectTriangle(glm::vec3 v1, glm::vec3 v2, glm::vec3 v3, Ray ray);
}
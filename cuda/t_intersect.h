#pragma once
#include "t_cudacommon.h"
#include "t_geometry.h"

#define INFINITY 0x1.fffffep+127f
#define EPSILON .0001f

struct Ray {
	glm::vec3 origin;
	glm::vec3 dir;
};

struct TriangleIntersection {
	float u; //Barycentric coordinate for v2
	float v; //Barycentric coordinate for v3
	float t{ INFINITY };
	float padding;
};

struct Intersection {
	glm::vec3 normal;
	int meshId{ -1 };
	int primitiveId{ -1 };
	float t{ INFINITY };
	float u;
	float v;
};

namespace tsukiintersect {
	//Slab-method ray/AABB test. Returns the entry distance t (in ray.dir units),
	//or INFINITY on a miss so callers comparing `t < minT` treat it as no hit.
	__host__ __device__ inline float intersectBoundingBox(Bounds bounds, Ray ray) {
		glm::vec3 invDir = 1.0f / ray.dir;
		glm::vec3 t0 = (bounds.min - ray.origin) * invDir;
		glm::vec3 t1 = (bounds.max - ray.origin) * invDir;

		glm::vec3 tNear = glm::min(t0, t1);
		glm::vec3 tFar = glm::max(t0, t1);

		float tmin = glm::max(tNear.x, glm::max(tNear.y, tNear.z));
		float tmax = glm::min(tFar.x, glm::min(tFar.y, tFar.z));

		//No overlap, or the box is entirely behind the ray origin -> miss.
		if (tmax < tmin || tmax < 0.0f) return INFINITY;

		return tmin; //May be < 0 if the origin is inside the box (still a hit).
	}

	//Moller-Trumbore ray/triangle test. Returns the hit distance t, or INFINITY
	//on a miss. The caller is responsible for the t > EPSILON near-plane check.
	__host__ __device__ inline TriangleIntersection intersectTriangle(glm::vec3 v1, glm::vec3 v2, glm::vec3 v3, Ray ray) { //TODO: tweak epsilon
		TriangleIntersection result{};

		glm::vec3 edge1 = v2 - v1;
		glm::vec3 edge2 = v3 - v1;

		glm::vec3 pvec = glm::cross(ray.dir, edge2);
		float det = glm::dot(edge1, pvec);

		//Ray parallel to the triangle plane (miss)
		if (det > -EPSILON && det < EPSILON) { return result; }

		float invDet = 1.0f / det;

		glm::vec3 tvec = ray.origin - v1;
		float u = glm::dot(tvec, pvec) * invDet;
		if (u < 0.f - EPSILON || u > 1.f + EPSILON) { return result; }

		glm::vec3 qvec = glm::cross(tvec, edge1);
		float v = glm::dot(ray.dir, qvec) * invDet;
		if (v < 0.f - EPSILON || u + v > 1.f + EPSILON) { return result; }

		glm::vec3 out = glm::cross(-edge2, edge1);
		if (glm::dot(out, ray.dir) > 0) { return result; } //If the vector out and the ray face the same direction, we're hitting the face from behind

		result.t = glm::dot(edge2, qvec) * invDet;
		result.u = u;
		result.v = v;
		return result;
	}
}
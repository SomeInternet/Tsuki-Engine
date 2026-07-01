#pragma once
#include "t_cudacommon.h"

namespace tsukisample {
	__device__ inline glm::vec2 toDiskUniform(glm::vec2 xi) { //Change to test performance?
		float r = __sqrtf(xi.x);
		float theta = 2.f * PI * xi.y;

		return glm::vec2(r * __cosf(theta), r * __sinf(theta));
	}

	__device__ inline glm::vec2 toDiskConcentric(glm::vec2 xi) {
		glm::vec2 uOffset = 2.f * xi - glm::vec2(1);

		if (uOffset.x == 0 && uOffset.y == 0) { return glm::vec2(0); }

		float r, theta;
		if (__fabsf(uOffset.x) > __fabsf(uOffset.y)) {
			r = uOffset.x;
			theta = (PI / 4.f) * (uOffset.y / uOffset.x);
		}
		else {
			r = uOffset.y;
			theta = (PI / 2.f) - (PI / 4.f) * (uOffset.x / uOffset.y);
		}

		return glm::vec2(r * __cosf(theta), r * __sinf(theta));
	}

	__device__ inline glm::vec3 toHemisphereCosineWeighted(glm::vec2 xi) {
		glm::vec2 diskSample = tsukisample::toDiskConcentric(xi);

		return glm::vec3(diskSample.x, diskSample.y, __fsqrtf(1.f - diskSample.x * diskSample.x - diskSample.y * diskSample.y));
	}

	__device__ inline glm::vec3 toHemisphereUniform(glm::vec2 xi) {
		float z = sample.x;
		float x = __cosf(2.f * PI * xi.y) * __sqrtf(1.f - z * z);
		float y = __sinf(2.f * PI * xi.y) * __sqrt(1.f - z * z);

		return glm::vec3(x, y, z);
	}

	__device__ inline glm::vec3 toSphereUniform(glm::vec2 xi) {
		float z = 1.f - 2.f * xi.x;
		float x = __cosf(2.f * PI * xi.y) * __sqrtf(1.f - z * z);
		float y = __sinf(2.f * PI * xi.y) * __sqrt(1.f - z * z);

		return glm::vec3(x, y, z);
	}
};
#pragma once
#include "t_cudacommon.h"

namespace tsukibsdf {
	__device__ inline float d(glm::vec3 wh, float roughness) {
		return 0.f;
	}

	__device__ inline float g(glm::vec3 wo, float roughness) {
		return 0.f;
	}

	__device__ inline glm::vec3 bsdfTorranceSparrow(glm::vec3 wo, glm::vec3 *wi, glm::vec3 albedo, float roughness) {
		
	}

	__device__ inline float pdfTorranceSparrow(glm::vec3 wo, glm::vec3 wh, float roughness) {
		//We're using VNDF (visible normal distribution function) sampling, which weights by the visibility of micronormals from wo
		//The g term denotes the fraction of visible microfacets when viewed from wo
		//dot(wo, wh) denotes the visibility of wh from wo
		//costhetao normalizes the pdf. The area of all visible microfacts projected onto the viewing direction equals the area of the macrosurface 
		//projected onto the viewing direction
		float alpha = roughness * roughness;

		float thetah = wh.z;
		float thetao = wo.z;

		float cosThetah = __cosf(thetah);
		float d = 1.f / (PI * alpha * alpha * __powf(cosThetah * cosThetah * cosThetah * cosThetah * (1.f + (0.f)), 4));

		return d(wh, roughness) * g(wo, roughness) * fabsf(glm::dot(wo, wh)) / fabsf(wo.z);
	}
};
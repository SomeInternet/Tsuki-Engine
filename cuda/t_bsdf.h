#pragma once
#include "t_cudacommon.h"

namespace tsukibsdf {
	__device__ inline float cos2Theta(glm::vec3 v) { return v.z * v.z; }

	__device__ inline float sin2Theta(glm::vec3 v) { return 1.f - cos2Theta(v); }

	__device__ inline float tan2Theta(glm::vec3 v) { return sin2Theta(v) / cos2Theta(v); }

	//TODO: Implement anisotropy (?)
	__device__ inline glm::vec3 sampleWh(float roughness, glm::vec3 wo, glm::vec2 xi) {
		//Perfectly specular
		if (roughness == 0.f) { return glm::reflect(-wo, glm::vec3(0, 0, 1)); }

		float alpha = roughness * roughness;

		glm::vec3 woStd = glm::normalize(glm::vec3(alpha * wo.x, alpha * wo.y, wo.z));

		float phi = 2.f * PI * xi.x;
		//Dupuy & Benyoub 2023 spherical-cap VNDF sampling: z = (1 - u.y)(1 + woStd.z) - woStd.z
		float z = fmaf(1.f - xi.y, 1.f + woStd.z, -woStd.z);
		float sinTheta = sqrtf(glm::clamp(1.f - z * z, 0.f, 1.f));
		glm::vec3 c = glm::vec3(sinTheta * __cosf(phi), sinTheta * sinf(phi), z);

		glm::vec3 whStd = c + woStd;

		return glm::normalize(glm::vec3(alpha * whStd.x, alpha * whStd.y, fmaxf(0.f, whStd.z)));
	}

	__device__ inline glm::vec3 bsdfTorranceSparrow(glm::vec3 wo, glm::vec3 wi, glm::vec3 albedo, float metallic, float roughness) {
		//Perfectly specular case
		if (roughness == 0.f) { return albedo * fabsf(wi.z); }

		//Wi goes below the horizon
		if (wi.z < 0.f) { return glm::vec3(0); }

		glm::vec3 wh = glm::normalize(wo + wi);

		float alpha = roughness * roughness;
		float alpha2 = alpha * alpha;

		float dotWoWh = fabsf(glm::dot(wo, wh));
		float dotNorWh = fabsf(wh.z); //We're in tangent space, so the normal is (0, 0, 1)

		//Head-on material reflectivity
		//glm::vec3 f0 = (1.f - metallic) * glm::vec3(.04f) + metallic * albedo;
		glm::vec3 f0 = albedo;

		//GGX D
		float d;
		{
			float tan2Wh = tan2Theta(wh);
			float cos4Wh = cos2Theta(wh) * cos2Theta(wh);

			float e = (cos2Theta(wh) / alpha2 + sin2Theta(wh) / alpha2) * tan2Wh;
			d = 1.f / (PI * alpha2 * cos4Wh * (1.f + e) * (1.f + e));
		}

		float g; //Smith-GGX
		{
			//Clamp cosines away from 0 so tan2 doesn't blow up to Inf at grazing angles
			float cosWo = fmaxf(fabsf(wo.z), 1e-4f);
			float cosWi = fmaxf(fabsf(wi.z), 1e-4f);

			float tan2Wo = (1.f - cosWo * cosWo) / (cosWo * cosWo);
			float lambdaWo = .5f * (-1.f + sqrtf(1.f + alpha2 * tan2Wo));

			float tan2Wi = (1.f - cosWi * cosWi) / (cosWi * cosWi);
			float lambdaWi = .5f * (-1.f + sqrtf(1.f + alpha2 * tan2Wi));

			g = 1.f / (1.f + lambdaWo + lambdaWi);
		}

		//Shlick approximation of Fresnel
		glm::vec3 f = f0 + (glm::vec3(1) - f0) * __powf(1.f - dotWoWh, 5.f);

		float cosThetaO = wo.z;
		float cosThetaI = wi.z;

		glm::vec3 ks = f;
		glm::vec3 kd = (1.f - metallic) * (glm::vec3(1) - ks);
		//return (d * g * f / (4.f * cosThetaO * cosThetaI)) + kd * (albedo / PI);

		//Clamp the denominator cosines away from 0 to avoid Inf/NaN at grazing angles
		float denom = 4.f * fmaxf(cosThetaO, 1e-4f) * fmaxf(cosThetaI, 1e-4f);
		return d * g * albedo / denom;
	}

	__device__ inline float pdfTorranceSparrow(float roughness, glm::vec3 wo, glm::vec3 wh) {
		//We're using VNDF (visible normal distribution function) sampling, which weights by the visibility of micronormals from wo
		//The g term denotes the fraction of visible microfacets when viewed from wo
		//dot(wo, wh) denotes the visibility of wh from wo
		//costhetao normalizes the pdf. The area of all visible microfacts projected onto the viewing direction equals the area of the macrosurface 
		//projected onto the viewing direction

		//Perfectly specular (Dirac delta distribution)
		if (roughness == 0.f) { return 1.f; }

		float alpha = roughness * roughness;
		float alpha2 = alpha * alpha;

		float dotNorWo = wo.z;
		float dotNorWh = wh.z;

		//Viewer below the horizon: 0/0 in g1 and the 1/(4*dotNorWo) normalization would give NaN
		if (dotNorWo <= 0.f) { return 0.f; }

		float g1 = 2.f * dotNorWo / (dotNorWo + sqrtf(alpha2 + (1.f - alpha2) * dotNorWo * dotNorWo));
		float denom = dotNorWh * dotNorWh * (alpha2 - 1.f) + 1.f;
		float d = alpha2 / (PI * denom * denom);

		return g1 * d / (4.f * dotNorWo);
	}
};
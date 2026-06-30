#pragma once

#define GLM_FORCE_CUDA
#include "glm/glm.hpp"

class TsukiCudaCamera; //Forward declaration

class TsukiCamera {
public:
	bool rotDirty{ true };
	bool viewDirty{ true };
	
	float radius{ 10.f };
	float velocity{ 1.f };
	glm::vec3 origin{ 0 };

	float sensitivity{ .01f };

	//Spherical polar coordinates
	float theta{ 0.f };
	float phi{ 0.f };

	const glm::mat4 &getView();
	const glm::mat4 &getRot();

	TsukiCudaCamera toCudaCamera();

	float fov{ 70.f }; //FOV in degrees

private:
	glm::mat4 viewMatrix;
	glm::mat4 rotMatrix;
};
#pragma once
#include "t_types.h"

class TsukiCamera {
public:
	bool rotDirty{ true };
	bool viewDirty{ true };
	
	float radius{ 5.f };
	float velocity{ 1.f };
	glm::vec3 origin{ 0 };

	float sensitivity{ .01f };

	//Spherical polar coordinates
	float theta{ 0.f };
	float phi{ 0.f };

	const glm::mat4 &getView();
	const glm::mat4 &getRot();

private:
	glm::mat4 viewMatrix;
	glm::mat4 rotMatrix;
};
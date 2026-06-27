#pragma once
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

//TODO: Remove, no longer needed
struct Vertex {
	//Structured for efficient memory alignment
	//Apparently, GPU's like aligning data structures to 4 byte slots
	glm::vec3 pos;
	float uvX;
	glm::vec3 normal;
	float uvY;
	glm::vec4 color;
};
#include "t_camera.h"
#include "t_engine.h"
#include "t_types.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>

const glm::mat4 &TsukiCamera::getView() { //TODO
	if (!viewDirty) { return viewMatrix; }
	
	viewDirty = false;
	viewMatrix = glm::translate(glm::vec3(0, 0, -radius)) * getRot() * glm::translate(-origin);
	return viewMatrix;
}

const glm::mat4 &TsukiCamera::getRot() {
	if (!rotDirty) { return rotMatrix; }

	rotDirty = false;
	rotMatrix = glm::rotate(glm::mat4(1), -theta, glm::vec3(1, 0, 0)) * glm::rotate(glm::mat4(1), -phi, glm::vec3(0, 1, 0));
	return rotMatrix;
}
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
	rotMatrix = glm::rotate(glm::rotate(glm::mat4(1), -phi, glm::vec3(0, 1, 0)), -theta, glm::vec3(1, 0, 0));
	return rotMatrix;
}

void TsukiCamera::mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
	TsukiInput &input = reinterpret_cast<TsukiEngine *>(glfwGetWindowUserPointer(window))->input;

	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		if (action == GLFW_PRESS) { input.mouseLeftHeld = true; }
		if (action == GLFW_RELEASE) { input.mouseLeftHeld = false; }
	}

	if (button == GLFW_MOUSE_BUTTON_RIGHT) {
		if (action == GLFW_PRESS) { input.mouseRightHeld = true; }
		if (action == GLFW_RELEASE) { input.mouseRightHeld = false; }
	}

	ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
}

void TsukiCamera::cursorPosCallback(GLFWwindow *window, double xPos, double yPos) {
	TsukiInput &input = reinterpret_cast<TsukiEngine *>(glfwGetWindowUserPointer(window))->input;
	TsukiCamera &camera = reinterpret_cast<TsukiEngine *>(glfwGetWindowUserPointer(window))->camera;

	if (input.firstMouse) {
		input.firstMouse = false;
		input.prevXPos = xPos;
		input.prevYPos = yPos;
		return;
	}

	float deltaX = static_cast<float>(input.prevXPos - xPos);
	float deltaY = static_cast<float>(input.prevYPos - yPos);

	if (input.mouseLeftHeld) {
		camera.rotDirty = true;
		camera.viewDirty = true;
		camera.theta = glm::clamp(camera.theta + deltaY * camera.sensitivity, -PI / 2 + EPSILON, PI / 2 - EPSILON);
		camera.phi = camera.phi + deltaX * camera.sensitivity;
	}

	if (input.mouseRightHeld) {
		camera.viewDirty = true;

		glm::vec3 right = glm::transpose(glm::mat3(camera.getRot())) * glm::vec3(1, 0, 0);
		glm::vec3 up = glm::transpose(glm::mat3(camera.getRot())) * glm::vec3(0, 1, 0);

		camera.origin += camera.sensitivity * (deltaX * right - deltaY * up);
	}

	input.prevXPos = xPos;
	input.prevYPos = yPos;

	ImGui_ImplGlfw_CursorPosCallback(window, xPos, yPos);
}

void TsukiCamera::scrollCallback(GLFWwindow *window, double xOffset, double yOffset) {
	TsukiInput &input = reinterpret_cast<TsukiEngine *>(glfwGetWindowUserPointer(window))->input;
	TsukiCamera &camera = reinterpret_cast<TsukiEngine *>(glfwGetWindowUserPointer(window))->camera;

	camera.viewDirty = true;
	camera.radius -= static_cast<float>(yOffset) * .1f; //Zoom

	ImGui_ImplGlfw_ScrollCallback(window, xOffset, yOffset);
}
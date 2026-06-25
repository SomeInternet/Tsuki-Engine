#include "t_input.h"
#include "t_engine.h"
#include "t_camera.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>

void TsukiInput::keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods) {
	TsukiInput &input = reinterpret_cast<TsukiEngine *>(glfwGetWindowUserPointer(window))->input;

	//TODO: process key inputs
	if (key == GLFW_KEY_RIGHT_SHIFT) {
		if (action == GLFW_PRESS) { input.cameraLocked = !input.cameraLocked; }
	}

	if (key == GLFW_KEY_LEFT_CONTROL) {
		if (action == GLFW_PRESS) { input.keyLCtrlHeld = true; }
		if (action == GLFW_RELEASE) { input.keyLCtrlHeld = false; }
	}

	ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
}

void TsukiInput::mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
	TsukiInput &input = reinterpret_cast<TsukiEngine *>(glfwGetWindowUserPointer(window))->input;

	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		if (action == GLFW_PRESS) { input.mouseLeftHeld = true; }
		if (action == GLFW_RELEASE) { input.mouseLeftHeld = false; }
	}

	if (button == GLFW_MOUSE_BUTTON_RIGHT) {
		if (action == GLFW_PRESS) { input.mouseRightHeld = true; }
		if (action == GLFW_RELEASE) { input.mouseRightHeld = false; }
	}

	if (!input.keyLCtrlHeld) {
		ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
	}
}

void TsukiInput::cursorPosCallback(GLFWwindow *window, double xPos, double yPos) {
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

	if (!input.cameraLocked && input.keyLCtrlHeld) {
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
	}
	
	input.prevXPos = xPos;
	input.prevYPos = yPos;

	if (!input.keyLCtrlHeld) {
		ImGui_ImplGlfw_CursorPosCallback(window, xPos, yPos);
	}
}

void TsukiInput::scrollCallback(GLFWwindow *window, double xOffset, double yOffset) {
	TsukiInput &input = reinterpret_cast<TsukiEngine *>(glfwGetWindowUserPointer(window))->input;
	TsukiCamera &camera = reinterpret_cast<TsukiEngine *>(glfwGetWindowUserPointer(window))->camera;

	if (!input.cameraLocked && input.keyLCtrlHeld) {
		camera.viewDirty = true;
		camera.radius -= static_cast<float>(yOffset) * .1f; //Zoom
	}

	if (!input.keyLCtrlHeld) {
		ImGui_ImplGlfw_ScrollCallback(window, xOffset, yOffset);
	}
}
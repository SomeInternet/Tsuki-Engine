#pragma once
#include "t_types.h"

struct TsukiInput {
	bool cameraLocked{ false }; //Kind of a bandaid fix, but oh well...

	bool firstMouse{ true };

	bool mouseLeftHeld{ false };
	bool mouseRightHeld{ false };

	double prevXPos{ 0 };
	double prevYPos{ 0 };

	float deltaTime{ .0001f };

	bool keyWHeld{ false };
	bool keyAHeld{ false };
	bool keySHeld{ false };
	bool keyDHeld{ false };
	bool keyLCtrlHeld{ false };

	static void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods);
	static void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods);
	static void cursorPosCallback(GLFWwindow *window, double xPos, double yPos);
	static void scrollCallback(GLFWwindow *window, double xOffset, double yOffset);
};
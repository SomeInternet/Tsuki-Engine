#pragma once
struct TsukiInput {
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
};
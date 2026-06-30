#include "t_engine.h"

int main(int argc, char *argv[]) {
	TsukiEngine engine;

	engine.init();

	engine.run();

	engine.cleanup();
}
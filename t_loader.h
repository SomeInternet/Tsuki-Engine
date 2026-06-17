#pragma once
#include "t_types.h"

#include <unordered_map>
#include <filesystem>

struct GLTFMaterial {
	TsukiMaterial data;
};

struct SubMesh {
	uint32_t offset;
	uint32_t size;

	std::shared_ptr<GLTFMaterial> material;
};

struct Mesh {
	std::string name;
	std::vector<SubMesh> subMeshes;
	GPUMeshBuffers meshBuffers;
};

class TsukiEngine; //Forward declaration

namespace tsukiutil {
	std::optional <std::vector<std::shared_ptr<Mesh>>> loadGltf(TsukiEngine *engine, std::filesystem::path filePath);
};
#pragma once
#include "t_types.h"

#include <unordered_map>
#include <filesystem>

//NOTE: The version of fastgltf used by VkGuide is outdated. I may want to use a more modern version...
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>

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

//TSUKIGLTF
//===================================================================================================================
struct TsukiGLTF {
	std::unordered_map<std::string, std::shared_ptr<Mesh>> meshes;
	std::unordered_map<std::string, std::shared_ptr<TNode>> nodes;
	std::unordered_map<std::string, AllocatedImage> images;
	std::unordered_map<std::string, std::shared_ptr<GLTFMaterial>> materials;

	std::vector<std::shared_ptr<TNode>> rootNodes;

	std::vector<VkSampler> samplers;

	DynamicDescriptorAllocator descriptorAllocator;
	AllocatedBuffer materialDataBuffer;

	TsukiEngine *engine;

	~TsukiGLTF() { clear(); }

	virtual void queueDraw(const glm::mat4 &matrix, TsukiDrawContext context);

private:
	void clear();
};

//TSUKIUTIL
//===================================================================================================================

namespace tsukiutil {
	std::optional <std::vector<std::shared_ptr<Mesh>>> loadGltf(TsukiEngine *engine, std::filesystem::path filePath);

	std::optional <std::shared_ptr<TsukiGLTF>> loadGltf2(TsukiEngine *engine, std::filesystem::path filePath);

	//GLTF LOADING HELPERS
	//===================================================================================================================
	VkFilter extractFilter(fastgltf::Filter filter); //Helper that determines the type of filter for a sampler
	VkSamplerMipmapMode extractMipmapMode(fastgltf::Filter filter);
};
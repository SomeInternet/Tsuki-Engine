#pragma once
#include "t_types.h"
#include "t_descriptors.h"
#include "t_bvh.h"

#include <unordered_map>
#include <filesystem>

//TODO: Change loadGLTF2 to load the glTF geometry into 1 giant vertex and index buffer
//So remove the vertices.clear(), indices.clear() for each submesh
//Might want to read into how GLTF's work...
//Do some refactoring as well
//I'll also need a parallel material lookup buffer

//NOTE: The version of fastgltf used by VkGuide is outdated. I may want to use a more modern version...
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>

struct TsukiMaterialData {
	int colorTextureIndex{ -1 };
	int metallicRoughnessTextureIndex{ -1 };
	int emissiveTextureIndex{ -1 };
	int normalTextureIndex{ -1 };

	glm::vec4 colorFac;
	glm::vec4 metallicRoughnessFac;
	glm::vec4 emissionFac;

	TsukiMaterialPass pass;
	uint8_t passPadding[15];

	glm::vec4 padding[11]; //Padding to meet the 256 bytes
};

struct TsukiMesh {
	std::string name;
	DeviceMeshBuffers meshBuffers;
	uint32_t indexCount{ 0 };
};

struct TsukiMeshInstance {
	int meshId;
	std::shared_ptr<TsukiMesh> mesh;
	glm::mat4 localTransform{ 1 };
	glm::mat4 inverseTranspose{ 1 };
};

class TsukiEngine; //Forward declaration

//TSUKIGLTF
//===================================================================================================================

struct TsukiGLTF {
	std::unordered_map<std::string, std::shared_ptr<TNode>> nodes;

	std::vector<std::shared_ptr<TNode>> rootNodes;
	std::vector<VkSampler> samplers;

	std::unordered_map<std::string, std::shared_ptr<TsukiMesh>> meshes;
	std::unordered_map<std::string, AllocatedImage> images;

	std::vector<BuildBVHNode> blasNodes;
	std::vector<BuildTLASNode> tlasNodes;

	TsukiEngine *engine;

	bool loadedToTlas{ false };

	~TsukiGLTF() { clear(); }

	virtual void queueDraw(const glm::mat4 &matrix, TsukiDrawContext &context);
private:
	void clear();
};

//TSUKIUTIL
//===================================================================================================================

namespace tsukiutil {
	std::optional<std::shared_ptr<TsukiGLTF>> loadGltf(TsukiEngine *engine, std::filesystem::path filePath);

	std::optional<AllocatedImage> loadGltfImage(TsukiEngine *engine, fastgltf::Asset &asset, fastgltf::Image &image);

	//GLTF LOADING HELPERS
	//===================================================================================================================
	VkFilter extractFilter(fastgltf::Filter filter); //Helper that determines the type of filter for a sampler
	VkSamplerMipmapMode extractMipmapMode(fastgltf::Filter filter);
};
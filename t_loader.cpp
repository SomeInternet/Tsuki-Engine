#include "stb_image.h"
#include <iostream>

#include "t_engine.h"
#include "t_loader.h"
#include "t_initializers.h"
#include "t_types.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

//NOTE: The version of fastgltf used by VkGuide is outdated. Consider updating?
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>

std::optional <std::vector<std::shared_ptr<Mesh>>> tsukiutil::loadGltf(TsukiEngine *engine, std::filesystem::path filePath) {
	std::cout << "Loading GLTF: " << filePath << std::endl;
	//std::cout << "  exists: " << std::filesystem::exists(filePath) << std::endl;

	fastgltf::GltfDataBuffer data;
	bool loaded = data.loadFromFile(filePath);
	//std::cout << "  loadFromFile returned: " << loaded << std::endl;

	constexpr auto gltfOptions = fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;

	fastgltf::Asset gltf;
	fastgltf::Parser parser{};

	//std::cout << "  calling loadBinaryGLTF..." << std::endl;
	auto load = parser.loadBinaryGLTF(&data, filePath.parent_path(), gltfOptions);
	//std::cout << "  loadBinaryGLTF returned, valid: " << (bool)load << std::endl;

	if (load) {
		gltf = std::move(load.get());
		//std::cout << "  mesh count: " << gltf.meshes.size() << std::endl;
	}
	else {
		std::cerr << "Failed to load gltf! " << fastgltf::to_underlying(load.error()) << std::endl;
		return{};
	}

	std::vector<std::shared_ptr<Mesh>> meshes;
	std::vector<uint32_t> indices;
	std::vector<Vertex> vertices;

	for (fastgltf::Mesh &mesh : gltf.meshes) {
		//std::cout << "  mesh: " << mesh.name << ", primitives: " << mesh.primitives.size() << std::endl;
		Mesh newMesh;
		newMesh.name = mesh.name;

		indices.clear();
		vertices.clear();

		//Iterate over the primitives of the mesh
		for (auto &&p : mesh.primitives) {
			SubMesh newSubMesh;
			newSubMesh.offset = static_cast<uint32_t>(indices.size());
			newSubMesh.size = static_cast<uint32_t>(gltf.accessors[p.indicesAccessor.value()].count);

			size_t initialVertex = vertices.size();

			//Load the indices
			fastgltf::Accessor &indexAccessor = gltf.accessors[p.indicesAccessor.value()];
			indices.reserve(indices.size() + indexAccessor.count);

			fastgltf::iterateAccessor<std::uint32_t>(gltf, indexAccessor, [&](std::uint32_t idx) {
				indices.push_back(idx + initialVertex);
				});

			std::cout << "    primitive attribute count: " << p.attributes.size() << std::endl;
			for (auto &attr : p.attributes) {
				//std::cout << "      attr: " << attr.first << std::endl;
			}

			//Load the vertex positions
			fastgltf::Accessor &posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];
			vertices.resize(vertices.size() + posAccessor.count);

			fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor, [&](glm::vec3 v, size_t idx) {
				Vertex newVertex;
				newVertex.pos = v;
				//Set the rest of the vertex attributes to dummy values
				newVertex.normal = glm::vec3(1, 0, 0);
				newVertex.color = glm::vec4(1);
				newVertex.uvX = 0;
				newVertex.uvY = 0;
				vertices[initialVertex + idx] = newVertex;
				});

			//Load vertex normals
			auto normals = p.findAttribute("NORMAL");
			if (normals != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).second], [&](glm::vec3 v, size_t idx) {
					vertices[initialVertex + idx].normal = v;
					});
			}

			//Load UVs
			auto uv = p.findAttribute("TEXCOORD_0");
			if (uv != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).second], [&](glm::vec2 v, size_t idx) {
					vertices[initialVertex + idx].uvX = v.x;
					vertices[initialVertex + idx].uvY = v.y;
					});
			}

			//Load colors
			auto colors = p.findAttribute("COLOR_0");
			if (colors != p.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).second], [&](glm::vec4 v, size_t idx) {
					vertices[initialVertex + idx].color = v;
					});
			}

			newMesh.subMeshes.push_back(newSubMesh);
		}

		//Display normals
		constexpr bool overrideColors = true;
		if (overrideColors) {
			for (Vertex &vertex : vertices) {
				vertex.color = glm::vec4(vertex.normal, 1.f);
			}
		}

		//Upload mesh buffers
		newMesh.meshBuffers = engine->uploadMesh(indices, vertices);
		//std::cout << "  uploaded mesh: " << newMesh.name << ", vertices: " << vertices.size() << ", indices: " << indices.size() << std::endl;
		meshes.emplace_back(std::make_shared<Mesh>(std::move(newMesh)));
	}

	return meshes;
}
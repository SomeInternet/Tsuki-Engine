#include "stb_image.h"
#include <iostream>

#include "t_engine.h"
#include "t_loader.h"
#include "t_initializers.h"
#include "t_types.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

//TSUKIGLTF
//===================================================================================================================
void TsukiGLTF::queueDraw(const glm::mat4 &matrix, TsukiDrawContext context) {
	for (auto &rootNode : rootNodes) {
		rootNode->queueDraw(matrix, context);
	}
}

void TsukiGLTF::clear() { //TODO: Verify
	meshes.clear();
	nodes.clear();
	images.clear();
	materials.clear();
	rootNodes.clear();
	samplers.clear();
}

//TSUKIUTIL
//===================================================================================================================
std::optional <std::vector<std::shared_ptr<Mesh>>> tsukiutil::loadGltf(TsukiEngine *engine, std::filesystem::path filePath) {
	std::cout << "Loading GLTF: " << filePath << std::endl;

	fastgltf::GltfDataBuffer data;
	bool loaded = data.loadFromFile(filePath);

	constexpr auto gltfOptions = fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;

	fastgltf::Asset gltf;
	fastgltf::Parser parser{};

	auto load = parser.loadBinaryGLTF(&data, filePath.parent_path(), gltfOptions);

	if (load) {
		gltf = std::move(load.get());
	}
	else {
		std::cerr << "Failed to load gltf! " << fastgltf::to_underlying(load.error()) << std::endl;
		return{};
	}

	std::vector<std::shared_ptr<Mesh>> meshes;
	std::vector<uint32_t> indices;
	std::vector<Vertex> vertices;

	for (fastgltf::Mesh &mesh : gltf.meshes) {
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
		constexpr bool overrideColors = false;
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

std::optional <std::shared_ptr<TsukiGLTF>> tsukiutil::loadGltf2(TsukiEngine *engine, std::filesystem::path filePath) { //TODO: replace loadGltf with this
	std::cerr << "Loading GLTF: " << filePath << std::endl;

	std::shared_ptr<TsukiGLTF> scene = std::make_shared<TsukiGLTF>();

	scene->engine = engine;

	TsukiGLTF &file = *scene.get();

	fastgltf::Parser parser;
	constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | 
		fastgltf::Options::AllowDouble | fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;

	fastgltf::GltfDataBuffer data;
	data.loadFromFile(filePath);

	fastgltf::Asset gltf;

	std::filesystem::path path = filePath;

	auto type = fastgltf::determineGltfFileType(&data);
	if (type == fastgltf::GltfType::glTF) {
		auto load = parser.loadGLTF(&data, path.parent_path(), gltfOptions);
		if (load) {
			gltf = std::move(load.get());
		}
		else {
			std::cerr << "Failed to load GLTF! Error: " << fastgltf::to_underlying(load.error()) << std::endl;
			return {};
		}
	}
	else if (type == fastgltf::GltfType::GLB) {
		auto load = parser.loadBinaryGLTF(&data, path.parent_path(), gltfOptions);
		if (load) {
			gltf = std::move(load.get());
		}
		else {
			std::cerr << "Failed to load GLTF! Error: " << fastgltf::to_underlying(load.error()) << std::endl;
			return {};
		}
	}
	else {
		std::cerr << "Failed to determine GLTF container" << std::endl;
		return {};
	}

	std::vector<DynamicDescriptorAllocator::PoolSizeRatio> sizes = { 
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
		{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}
	};

	file.descriptorAllocator.init(engine->_device, gltf.materials.size(), sizes);

	for (fastgltf::Sampler &sampler : gltf.samplers) {
		VkSamplerCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		info.maxLod = VK_LOD_CLAMP_NONE;
		info.minLod = 0;

		info.magFilter = tsukiutil::extractFilter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
		info.minFilter = tsukiutil::extractFilter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

		info.mipmapMode = tsukiutil::extractMipmapMode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

		VkSampler sampler;
		VK_CHECK(vkCreateSampler(engine->_device, &info, nullptr, &sampler));
		file.samplers.push_back(sampler);
	}

	std::vector<std::shared_ptr<Mesh>> meshes;
	std::vector<std::shared_ptr<TNode>> nodes;
	std::vector<AllocatedImage> images;
	std::vector<std::shared_ptr<GLTFMaterial>> materials;

	//Load textures
	for (fastgltf::Image &image : gltf.images) {
		//TODO: update
		
		//Temporary
		images.push_back(engine->_errorCheckerboardImage);
	}

	//Load material data into a buffer
	//Create buffer
	//Material constants holds information like the color factor and PBR properties
	file.materialDataBuffer = engine->createBuffer(sizeof(GLTFMetallicRoughness::MaterialConstants) * gltf.materials.size(),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	int dataIndex = 0;
	GLTFMetallicRoughness::MaterialConstants *sceneMaterialConstants = reinterpret_cast<GLTFMetallicRoughness::MaterialConstants *>(file.materialDataBuffer.info.pMappedData);
	for (fastgltf::Material &material : gltf.materials) {
		std::shared_ptr<GLTFMaterial> newMaterial = std::make_shared<GLTFMaterial>();
		materials.push_back(newMaterial);
		file.materials[material.name.c_str()] = newMaterial;

		GLTFMetallicRoughness::MaterialConstants constants;
		constants.colorFac.r = material.pbrData.baseColorFactor[0];
		constants.colorFac.g = material.pbrData.baseColorFactor[1];
		constants.colorFac.b = material.pbrData.baseColorFactor[2];
		constants.colorFac.a = material.pbrData.baseColorFactor[3];

		constants.metallicRoughnessFac.x = material.pbrData.metallicFactor;
		constants.metallicRoughnessFac.y = material.pbrData.roughnessFactor;

		sceneMaterialConstants[dataIndex] = constants; //Write it into the buffer

		TsukiMaterialPass passType = (material.alphaMode == fastgltf::AlphaMode::Blend) ? TsukiMaterialPass::TSUKI_MATERIAL_TRANSPARENT : TsukiMaterialPass::TSUKI_MATERIAL_OPAQUE;

		//Set the material resources to the defaults for now
		GLTFMetallicRoughness::MaterialResources materialResources;
		materialResources.colorImage = engine->_whiteImage;
		materialResources.colorSampler = engine->_defaultSamplerLinear;
		materialResources.metallicRoughnessImage = engine->_whiteImage;
		materialResources.metallicRoughnessSampler = engine->_defaultSamplerLinear;

		materialResources.dataBuffer = file.materialDataBuffer.buffer;
		materialResources.dataBufferOffset = dataIndex * sizeof(GLTFMetallicRoughness::MaterialConstants);

		if (material.pbrData.baseColorTexture.has_value()) {
			size_t image = gltf.textures[material.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
			size_t sampler = gltf.textures[material.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value();

			//TODO: Parse for PBR textures as well
			materialResources.colorImage = images[image];
			materialResources.colorSampler = file.samplers[sampler];
		}

		newMaterial->data = engine->metallicRoughnessMaterial.writeMaterial(engine->_device, passType, materialResources, file.descriptorAllocator);
		++dataIndex;
	}

	//Load the meshes
	std::vector<uint32_t> indices;
	std::vector<Vertex> vertices;
	for (fastgltf::Mesh &mesh : gltf.meshes) {
		std::shared_ptr<Mesh> newMesh = std::make_shared<Mesh>();
		meshes.push_back(newMesh);
		file.meshes[mesh.name.c_str()] = newMesh;
		newMesh->name = mesh.name;

		indices.clear();
		vertices.clear();
		for (auto &primitive : mesh.primitives) {
			SubMesh subMesh;
			subMesh.offset = static_cast<uint32_t>(indices.size());
			subMesh.size = static_cast<uint32_t>(gltf.accessors[primitive.indicesAccessor.value()].count);

			size_t initialVertex = vertices.size(); //Get the next index that
			//Load indices
			{
				fastgltf::Accessor &indexAccessor = gltf.accessors[primitive.indicesAccessor.value()];
				indices.reserve(indices.size() + indexAccessor.count);

				fastgltf::iterateAccessor<std::uint32_t>(gltf, indexAccessor, [&](std::uint32_t index) {
					indices.push_back(index + initialVertex);
					});
			}

			//Load the vertex positions
			fastgltf::Accessor &posAccessor = gltf.accessors[primitive.findAttribute("POSITION")->second];
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
			auto normals = primitive.findAttribute("NORMAL");
			if (normals != primitive.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).second], [&](glm::vec3 v, size_t idx) {
					vertices[initialVertex + idx].normal = v;
					});
			}

			//Load UVs
			auto uv = primitive.findAttribute("TEXCOORD_0");
			if (uv != primitive.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).second], [&](glm::vec2 v, size_t idx) {
					vertices[initialVertex + idx].uvX = v.x;
					vertices[initialVertex + idx].uvY = v.y;
					});
			}

			//Load colors
			auto colors = primitive.findAttribute("COLOR_0");
			if (colors != primitive.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).second], [&](glm::vec4 v, size_t idx) {
					vertices[initialVertex + idx].color = v;
					});
			}

			if (primitive.materialIndex.has_value()) {
				subMesh.material = materials[primitive.materialIndex.value()];
			}
			else {
				subMesh.material = materials[0];
			}

			newMesh->subMeshes.push_back(subMesh);
		}
		newMesh->meshBuffers = engine->uploadMesh(indices, vertices);
	}

	//Load the nodes
	for (fastgltf::Node &node : gltf.nodes) {
		std::shared_ptr<TNode> newNode;

		if (node.meshIndex.has_value()) { //If it has a mesh, assign it the corresponding mesh based on its index
			newNode = std::make_shared<TMeshNode>();
			static_cast<TMeshNode *>(newNode.get())->mesh = meshes[*node.meshIndex];
		}
		else {
			newNode = std::make_shared<TNode>();
		}

		nodes.push_back(newNode);
		file.nodes[node.name.c_str()] = newNode;

		//NOTE: std::visit allows us to execute a callable object (e.g. a lambda) on the currently active type inside an std::variant
		//A variant is a type-safe version of a C-style union. A union packs all of its fields into the same memory location

		//In glTF 2.0, a scene node can define its local transformation as individual lot, rot, scale (TRS) properties, or as a
		//4x4 transformation matrix, but not both, so it's stored like a variant
		std::visit(fastgltf::visitor{ [&](fastgltf::Node::TransformMatrix matrix) {
				memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
			},
			[&](fastgltf::Node::TRS transform) {
				glm::vec3 trans = glm::vec3(transform.translation[0], transform.translation[1], transform.translation[2]);
				glm::quat rot = glm::quat(transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]);
				glm::vec3 scale = glm::vec3(transform.scale[0], transform.scale[1], transform.scale[2]);

				glm::mat4 transMat = glm::translate(trans);
				glm::mat4 rotMat = glm::toMat4(rot);
				glm::mat4 scaleMat = glm::scale(scale);

				newNode->localTransform = transMat * rotMat * scaleMat;
			} }, node.transform);
	}

	//Set up the scene graph hierarchy
	for (int i = 0; i < gltf.nodes.size(); ++i) {
		fastgltf::Node &gltfNode = gltf.nodes[i];

		std::shared_ptr<TNode> &sceneNode = nodes[i];

		for (auto &child : gltfNode.children) {
			sceneNode->children.push_back(nodes[child]);
			nodes[child]->parent = sceneNode;
		}

		//Find the root nodes
		for (auto &node : nodes) {
			if (node->parent.lock() == nullptr) {
				//The advantage that weak pointers have over raw pointers is protection against dangling
				//To read or modify the data, you must temporarily promote the weak pointer to a shared pointer via .lock()
				//.lock() returns nullptr if the resource pointed to no longer exists
				//You can therefore use .lock() to check if the resource is still alive
				file.rootNodes.push_back(node);
				node->updateTransform(glm::mat4(1));
			}
		}
	}

	return scene;
}

//GLTF LOADING HELPERS
//===================================================================================================================
VkFilter tsukiutil::extractFilter(fastgltf::Filter filter) {
	switch (filter) {
		//Nearest sampler
	case fastgltf::Filter::Nearest:
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::NearestMipMapLinear:
		return VK_FILTER_NEAREST;

		//Linear sampler
	case fastgltf::Filter::Linear:
	case fastgltf::Filter::LinearMipMapNearest:
	case fastgltf::Filter::LinearMipMapLinear:
	default:
		return VK_FILTER_LINEAR;
	}
}

VkSamplerMipmapMode tsukiutil::extractMipmapMode(fastgltf::Filter filter) {
	switch (filter) {
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::LinearMipMapNearest:
		return VK_SAMPLER_MIPMAP_MODE_NEAREST;

	case fastgltf::Filter::NearestMipMapLinear:
	case fastgltf::Filter::LinearMipMapLinear:
	default:
		return VK_SAMPLER_MIPMAP_MODE_LINEAR;
	}
}
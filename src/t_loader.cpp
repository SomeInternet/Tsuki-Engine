#include <iostream>

#include "t_engine.h"
#include "t_loader.h"
#include "t_initializers.h"
#include "t_types.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

//TSUKIGLTF
//===================================================================================================================
void TsukiGLTF::queueDraw(const glm::mat4 &matrix, TsukiDrawContext &context) {
	if (!loadedToTlas && context.engine->_renderMode == TsukiRenderMode::TSUKI_RENDER_MODE_PATHTRACED_CUDA) {
		loadedToTlas = true;
		engine->_tlasDirty = true;
	}

	for (auto &rootNode : rootNodes) {
		rootNode->queueDraw(matrix, context);
	}
}

void TsukiGLTF::clear() { //TODO: Verify
	VkDevice device = engine->_device;

	//Deallocate all the buffers for the meshes
	for (auto &[key, value] : meshes) {
		engine->destroyBuffer(value->meshBuffers.indexBuffer);
		engine->destroyBuffer(value->meshBuffers.posBuffer);
		engine->destroyBuffer(value->meshBuffers.normalBuffer);
		engine->destroyBuffer(value->meshBuffers.colorBuffer);
		engine->destroyBuffer(value->meshBuffers.uvBuffer);
		engine->destroyBuffer(value->meshBuffers.materialLookupBuffer);
	}

	//Destroy the images
	for (auto &[key, value] : images) {
		if (value.image == engine->_errorCheckerboardImage.image) { continue; } //Excepting the error checkerboard, which belongs to the engine

		engine->destroyImage(value);
	}
}

//TSUKIUTIL
//===================================================================================================================
std::optional <std::shared_ptr<TsukiGLTF>> tsukiutil::loadGltf(TsukiEngine *engine, std::filesystem::path filePath) {
	std::cerr << "Loading glTF: " << filePath << std::endl;

	std::shared_ptr<TsukiGLTF> scene = std::make_shared<TsukiGLTF>();

	scene->engine = engine;

	int baseMeshIndex = engine->_blas.size();

	TsukiGLTF &file = *scene.get();

	constexpr auto gltfExtensions = fastgltf::Extensions::KHR_materials_emissive_strength;

	fastgltf::Parser parser(gltfExtensions);
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

	std::vector<std::shared_ptr<TsukiMesh>> meshes;
	std::vector<std::shared_ptr<TNode>> nodes;
	std::vector<AllocatedImage> images;

	std::vector<AllocatedImage> loadedImages;
	loadedImages.reserve(gltf.images.size());

	//TODO: Make sure the images are loaded to an exportable image
	for (fastgltf::Image &image : gltf.images) {
		std::optional<AllocatedImage> texture = tsukiutil::loadGltfImage(engine, gltf, image);

		if (texture.has_value()) {
			loadedImages.push_back(texture.value());
		}
		else {
			loadedImages.push_back(engine->_errorCheckerboardImage);
			std::cerr << "Failed to load glTF image texture: " << image.name << std::endl;
		}
	}

	//Track where THIS specific glTF asset's texture array begins in the global engine array
	int baseTextureIndex = static_cast<int>(engine->materialTextures.size());

	//Push actual glTF textures (which map to our loaded images) into the engine
	//Textures are images and samplers.
	for (fastgltf::Texture &texture : gltf.textures) {
		AllocatedImage matTexture;

		// Fallback check in case a glTF texture doesn't define an image index
		if (texture.imageIndex.has_value()) {
			matTexture = loadedImages[texture.imageIndex.value()];
		}
		else {
			matTexture = engine->_errorCheckerboardImage;
		}

		engine->materialTextures.push_back(matTexture);
	}

	//Load materials
	int baseMaterialIndex = static_cast<int>(engine->materials.size());
	for (fastgltf::Material &material : gltf.materials) {
		TsukiMaterialData newMat;

		newMat.colorFac = glm::vec4(
			material.pbrData.baseColorFactor[0],
			material.pbrData.baseColorFactor[1],
			material.pbrData.baseColorFactor[2],
			material.pbrData.baseColorFactor[3]);

		newMat.metallicRoughnessFac = glm::vec4(
			material.pbrData.metallicFactor,
			material.pbrData.roughnessFactor,
			0.0f, 0.0f);

		newMat.emissionFac = glm::vec4(
			material.emissiveFactor[0],
			material.emissiveFactor[1],
			material.emissiveFactor[2],
			material.emissiveStrength.has_value() ? material.emissiveStrength.value() : 0.f);

		newMat.pass = (material.alphaMode == fastgltf::AlphaMode::Blend) ?
			TsukiMaterialPass::TSUKI_MATERIAL_TRANSPARENT : TsukiMaterialPass::TSUKI_MATERIAL_OPAQUE;

		//Map the material textures to the texture index in the global descriptor array.
		if (material.pbrData.baseColorTexture.has_value()) {
			newMat.colorTextureIndex = baseTextureIndex + static_cast<int>(material.pbrData.baseColorTexture.value().textureIndex);
		}
		if (material.pbrData.metallicRoughnessTexture.has_value()) {
			newMat.metallicRoughnessTextureIndex = baseTextureIndex + static_cast<int>(material.pbrData.metallicRoughnessTexture.value().textureIndex);
		}
		if (material.normalTexture.has_value()) {
			newMat.normalTextureIndex = baseTextureIndex + static_cast<int>(material.normalTexture.value().textureIndex);
		}
		if (material.emissiveTexture.has_value()) {
			newMat.emissiveTextureIndex = baseTextureIndex + static_cast<int>(material.emissiveTexture.value().textureIndex);
		}

		engine->materials.push_back(newMat);
	}

	//Load the meshes
	std::vector<uint32_t> indices;
	std::vector<glm::vec4> positions;
	std::vector<glm::vec4> normals;
	std::vector<glm::vec4> colors;
	std::vector<glm::vec2> uvs;
	std::vector<uint32_t> materialLookup;
	for (fastgltf::Mesh &mesh : gltf.meshes) {
		std::shared_ptr<TsukiMesh> newMesh = std::make_shared<TsukiMesh>();
		meshes.push_back(newMesh);
		engine->meshes.push_back(newMesh);
		file.meshes[mesh.name.c_str()] = newMesh;
		newMesh->name = mesh.name;

		indices.clear();
		positions.clear();
		normals.clear();
		colors.clear();
		uvs.clear();
		materialLookup.clear();

		for (auto &primitive : mesh.primitives) {
			int materialIndex = primitive.materialIndex.has_value()
				? baseMaterialIndex + primitive.materialIndex.value()
				: 0;

			size_t initialVertex = positions.size();
			{
				fastgltf::Accessor &indexAccessor = gltf.accessors[primitive.indicesAccessor.value()];
				indices.reserve(indices.size() + indexAccessor.count);

				fastgltf::iterateAccessor<std::uint32_t>(gltf, indexAccessor, [&](std::uint32_t index) {
					indices.push_back(index + initialVertex);
					});
			}

			fastgltf::Accessor &posAccessor = gltf.accessors[primitive.findAttribute("POSITION")->second];
			positions.resize(positions.size() + posAccessor.count);
			normals.resize(positions.size(), glm::vec4(1, 0, 0, 0));
			colors.resize(positions.size(), glm::vec4(1));
			uvs.resize(positions.size(), glm::vec2(0));

			fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor, [&](glm::vec3 v, size_t idx) {
				positions[initialVertex + idx] = glm::vec4(v, 1);
				});

			auto normalsAttr = primitive.findAttribute("NORMAL");
			if (normalsAttr != primitive.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normalsAttr).second], [&](glm::vec3 v, size_t idx) {
					normals[initialVertex + idx] = glm::vec4(v, 0);
					});
			}

			auto uv = primitive.findAttribute("TEXCOORD_0");
			if (uv != primitive.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).second], [&](glm::vec2 v, size_t idx) {
					uvs[initialVertex + idx] = v;
					});
			}

			auto colorsAttr = primitive.findAttribute("COLOR_0");
			if (colorsAttr != primitive.attributes.end()) {
				fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colorsAttr).second], [&](glm::vec4 v, size_t idx) {
					colors[initialVertex + idx] = v;
					});
			}

			//Assign material index to each vertex of this primitive
			materialLookup.resize(positions.size(), static_cast<uint32_t>(materialIndex));
		}

		//std::cerr << "Uploading mesh..." << std::endl;
		newMesh->indexCount = static_cast<uint32_t>(indices.size());
		newMesh->meshBuffers = engine->uploadMesh(indices, positions, normals, colors, uvs, materialLookup, baseMaterialIndex);

		//Build the BVH for the mesh
		engine->_blas.push_back(tsukibvh::buildBVH(indices, positions, BVHHeuristic::SURFACE_AREA));
	}

	//Load the nodes
	for (fastgltf::Node &node : gltf.nodes) {
		std::shared_ptr<TNode> newNode;

		if (node.meshIndex.has_value()) { //If it has a mesh, assign it the corresponding mesh based on its index
			newNode = std::make_shared<TMeshNode>();

			reinterpret_cast<TMeshNode *>(newNode.get())->blasId = node.meshIndex.value() + baseMeshIndex;
			//instance.transform = node.transform;
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

	//std::cerr << "Finished loading glTF..." << std::endl;
	return scene;
}

std::optional<AllocatedImage> tsukiutil::loadGltfImage(TsukiEngine *engine, fastgltf::Asset &asset, fastgltf::Image &image) {
	AllocatedImage newImage{};

	int width, height, numChannels;

	std::visit(
		fastgltf::visitor{ [](auto &arg) {},
		[&](fastgltf::sources::URI &filePath) { //Case 1) Texture stored outside of the glTF/glb file
			assert(filePath.fileByteOffset == 0); //No stbi offsets
			assert(filePath.uri.isLocalPath());

			const std::string path(filePath.uri.path().begin(), filePath.uri.path().end());
			unsigned char *data = stbi_load(path.c_str(), &width, &height, &numChannels, 4); //Read in the image data to a buffer

			if (data) {
				VkExtent3D extent;
				extent.width = width;
				extent.height = height;
				extent.depth = 1;

				newImage = engine->createExternalImage(data, extent, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);

				stbi_image_free(data);
			}
		},
		[&](fastgltf::sources::Vector &vector) { //Case 2) Fastgltf read in the file to a vector
			unsigned char *data = stbi_load_from_memory(vector.bytes.data(), static_cast<int>(vector.bytes.size()), &width, &height, &numChannels, 4);

			if (data) {
				VkExtent3D extent;
				extent.width = width;
				extent.height = height;
				extent.depth = 1;

				newImage = engine->createExternalImage(data, extent, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, false);
			}
		},
		[&](fastgltf::sources::BufferView &view) { //Case 3) Image is embedded in a binary glb file
			auto &bufferView = asset.bufferViews[view.bufferViewIndex];
			auto &buffer = asset.buffers[bufferView.bufferIndex];

			std::visit(fastgltf::visitor{ [](auto &arg) {},
				[&](fastgltf::sources::Vector &vector) {
					unsigned char *data = stbi_load_from_memory(vector.bytes.data() + bufferView.byteOffset,
						static_cast<int>(bufferView.byteLength),
						&width, &height, &numChannels, 4);
					if (data) {
						VkExtent3D imagesize;
						imagesize.width = width;
						imagesize.height = height;
						imagesize.depth = 1;

						newImage = engine->createExternalImage(data, imagesize, VK_FORMAT_R8G8B8A8_UNORM,
							VK_IMAGE_USAGE_SAMPLED_BIT,false);

						stbi_image_free(data);
					}
				} },
				buffer.data);
		},
	}, image.data);

	if (newImage.image == VK_NULL_HANDLE) { return{}; }

	return newImage;
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
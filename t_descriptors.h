#pragma once
#include "t_types.h"

struct DescriptorLayoutBuilder {
	std::vector<VkDescriptorSetLayoutBinding> bindings;

	void addBinding(uint32_t binding, VkDescriptorType type);
	void clear();
	VkDescriptorSetLayout build(VkDevice device, VkShaderStageFlags shaderStages, void *pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
};

struct DescriptorAllocator { //TODO: Complete
	struct PoolSizeRatio {
		VkDescriptorType type;
		float ratio; //Ratio of each descriptor to descriptor set
	};

	VkDescriptorPool pool;

	void initPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
	void clearDescriptors(VkDevice device);
	void destroyPool(VkDevice device);

	VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout);
};

//DYNAMICDESCRIPTORALLOCATOR
//===================================================================================================================
class DynamicDescriptorAllocator {
public:
	//Same public interface as the descriptor layout builder
	struct PoolSizeRatio {
		VkDescriptorType type;
		float ratio; //Ratio of the descriptor to descriptor sets
	};

	void init(VkDevice device, uint32_t initialSets, std::span<PoolSizeRatio> poolRatios);
	void clearPools(VkDevice device);
	void destroyPools(VkDevice device);

	VkDescriptorSet allocate(VkDevice device, VkDescriptorSetLayout layout, void *pNext = nullptr);

private:
	//When we allocate, we'll try to grab a pool from the ready pools. If allocation succeeds, we return the pool back
	//to ready pools. If it fails, we place it into the full pools vector and attempt to get another pool. Get pool will
	//create a new pool if all pools are filled
	VkDescriptorPool getPool(VkDevice device);
	VkDescriptorPool createPool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios);

	std::vector<PoolSizeRatio> ratios;
	std::vector<VkDescriptorPool> fullPools;
	std::vector<VkDescriptorPool> readyPools;

	uint32_t setsPerPool;
};

//DESCRIPTORWRITER
//===================================================================================================================
struct DescriptorWriter {
	std::deque<VkDescriptorImageInfo> imageInfos;
	std::deque<VkDescriptorBufferInfo> bufferInfos;
	std::vector<VkWriteDescriptorSet> writes;

	void writeImage(int binding, VkImageView imageView, VkSampler sampler, VkImageLayout layout, VkDescriptorType type);
	void writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type);

	void clear();
	void updateSet(VkDevice device, VkDescriptorSet set);
};
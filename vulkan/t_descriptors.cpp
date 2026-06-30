#include "t_descriptors.h"

void DescriptorLayoutBuilder::addBinding(uint32_t binding, VkDescriptorType type, uint32_t descriptorCount) {
	VkDescriptorSetLayoutBinding newBinding{};
	newBinding.binding = binding;
	newBinding.descriptorCount = descriptorCount;
	newBinding.descriptorType = type;

	bindings.push_back(newBinding);
}

void DescriptorLayoutBuilder::clear() {
	bindings.clear();
}

VkDescriptorSetLayout DescriptorLayoutBuilder::build(VkDevice device, VkShaderStageFlags shaderStages, void *pNext /*= nullptr*/ , VkDescriptorSetLayoutCreateFlags flags /*= 0*/ ) {
	for (auto &binding : bindings) {
		binding.stageFlags |= shaderStages;
	}

	VkDescriptorSetLayoutCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	info.pNext = pNext;
	info.pBindings = bindings.data();
	info.bindingCount = static_cast<uint32_t>(bindings.size());
	info.flags = flags;

	VkDescriptorSetLayout set;
	VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set)); //Descriptor set layouts make a "promise" to Vulkan about what resources will be available and when

	return set;
}

void DescriptorAllocator::initPool(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios) {
	std::vector<VkDescriptorPoolSize> poolSizes;
	for (PoolSizeRatio ratio : poolRatios) {
		VkDescriptorPoolSize poolSize{};
		poolSize.type = ratio.type;
		poolSize.descriptorCount = static_cast<uint32_t>(ratio.ratio * maxSets);

		poolSizes.push_back(poolSize);
	}

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = 0;
	poolInfo.maxSets = maxSets;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();

	vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool);
}

void DescriptorAllocator::clearDescriptors(VkDevice device) {
	vkResetDescriptorPool(device, pool, 0);
}

void DescriptorAllocator::destroyPool(VkDevice device) {
	vkDestroyDescriptorPool(device, pool, nullptr);
}

VkDescriptorSet DescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout) {
	VkDescriptorSetAllocateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	info.pNext = nullptr;
	info.descriptorPool = pool;
	info.descriptorSetCount = 1;
	info.pSetLayouts = &layout;

	VkDescriptorSet descriptorSet;
	VK_CHECK(vkAllocateDescriptorSets(device, &info, &descriptorSet));
	return descriptorSet;
}

//DYNAMICDESCRIPTORALLOCATOR
//===================================================================================================================
//The dynamic descriptor allocator makes room for descriptor set allocations by dynamically allocating descriptor pools
//of increasing size, then allocating descriptors from them while they're not full, rinse and repeat
VkDescriptorPool DynamicDescriptorAllocator::getPool(VkDevice device) {
	VkDescriptorPool newPool;
	if (readyPools.size()) { //Still ready pools--some pool might still not be full
		newPool = readyPools.back();
		readyPools.pop_back();
	}
	else { //All pools are filled; we need to create a new pool
		newPool = createPool(device, setsPerPool, ratios);

		//Grow the number of sets per pool in anticipation of needing to allocate more sets in the future--allocation is expensive
		setsPerPool *= 1.5;
		setsPerPool = (setsPerPool > 4092) ? 4092 : setsPerPool;
	}
	return newPool;
}

VkDescriptorPool DynamicDescriptorAllocator::createPool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios, VkDescriptorPoolCreateFlags flags /*=0*/ ) {
	std::vector<VkDescriptorPoolSize> poolSizes;
	for (PoolSizeRatio ratio : poolRatios) {
		VkDescriptorPoolSize newPool{};
		newPool.type = ratio.type;
		newPool.descriptorCount = static_cast<uint32_t>(ratio.ratio * setCount);

		poolSizes.push_back(newPool);
	}

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = this->flags;
	poolInfo.maxSets = setCount;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();

	VkDescriptorPool newPool;
	vkCreateDescriptorPool(device, &poolInfo, nullptr, &newPool);
	return newPool;
}

void DynamicDescriptorAllocator::init(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios, VkDescriptorPoolCreateFlags flags /*= 0*/) {
	ratios.clear();
	this->flags = flags;

	for (auto ratio : poolRatios) {
		ratios.push_back(ratio);
	}

	VkDescriptorPool newPool = createPool(device, maxSets, poolRatios, flags);
	setsPerPool = maxSets * 1.5;

	readyPools.push_back(newPool);
}

void DynamicDescriptorAllocator::clearPools(VkDevice device) {
	for (auto pool : readyPools) {
		vkResetDescriptorPool(device, pool, 0);
	}

	for (auto pool : fullPools) {
		vkResetDescriptorPool(device, pool, 0);
		readyPools.push_back(pool);
	}
	fullPools.clear();
}

void DynamicDescriptorAllocator::destroyPools(VkDevice device) {
	for (auto pool : readyPools) {
		vkDestroyDescriptorPool(device, pool, 0);
	}
	readyPools.clear();

	for (auto pool : fullPools) {
		vkDestroyDescriptorPool(device, pool, 0);
	}
	fullPools.clear();
}

VkDescriptorSet DynamicDescriptorAllocator::allocate(VkDevice device, VkDescriptorSetLayout layout, void *pNext) {
	VkDescriptorPool pool = getPool(device);

	//Use the layout and number of sets to allocate some number of descriptor sets (1, in our case)
	VkDescriptorSetAllocateInfo info{};
	info.pNext = pNext;
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	info.descriptorPool = pool;
	info.descriptorSetCount = 1;
	info.pSetLayouts = &layout;

	VkDescriptorSet descriptorSet;
	VkResult result = vkAllocateDescriptorSets(device, &info, &descriptorSet);

	if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
		fullPools.push_back(pool);
		pool = getPool(device);

		VK_CHECK(vkAllocateDescriptorSets(device, &info, &descriptorSet));
	}

	readyPools.push_back(pool);
	return descriptorSet;
}

//DESCRIPTORWRITER
//===================================================================================================================
void DescriptorWriter::writeImage(int binding, VkImageView imageView, VkSampler sampler, VkImageLayout layout, VkDescriptorType type) {
	VkDescriptorImageInfo &info = imageInfos.emplace_back(VkDescriptorImageInfo{
		.sampler = sampler,
		.imageView = imageView,
		.imageLayout = layout
		});

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = VK_NULL_HANDLE;
	write.dstBinding = binding;
	write.descriptorType = type;
	write.pImageInfo = &info;
	write.descriptorCount = 1;

	writes.push_back(write);
}

void DescriptorWriter::writeImageArray(int binding, VkImageView *imageViews, uint32_t count, VkSampler sampler, VkImageLayout layout, VkDescriptorType type) {
	size_t startIdx = imageInfos.size();
	for (uint32_t i = 0; i < count; ++i) {
		imageInfos.emplace_back(VkDescriptorImageInfo{
			.sampler = sampler,
			.imageView = imageViews[i],
			.imageLayout = layout
			});
	}

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = VK_NULL_HANDLE;
	write.dstBinding = binding;
	write.descriptorType = type;
	write.pImageInfo = &imageInfos[startIdx];
	write.descriptorCount = count;

	writes.push_back(write);
}

void DescriptorWriter::writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type) {
	VkDescriptorBufferInfo &info = bufferInfos.emplace_back(VkDescriptorBufferInfo{
		.buffer = buffer,
		.offset = offset,
		.range = size
		});

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = VK_NULL_HANDLE;
	write.dstBinding = binding;
	write.descriptorType = type;
	write.pBufferInfo = &info;
	write.descriptorCount = 1;

	writes.push_back(write);
}

void DescriptorWriter::clear() {
	imageInfos.clear();
	bufferInfos.clear();
	writes.clear();
}

void DescriptorWriter::updateSet(VkDevice device, VkDescriptorSet set) {
	for (VkWriteDescriptorSet &write : writes) {
		write.dstSet = set;
	}

	vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}
#include "t_descriptors.h"

void DescriptorLayoutBuilder::addBinding(uint32_t binding, VkDescriptorType type) {
	VkDescriptorSetLayoutBinding newBinding{};
	newBinding.binding = binding;
	newBinding.descriptorCount = 1;
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

Tsuki Engine
=====================
T Fong `SomeInternetBoi`  
[My Website](tzfong.com), [My LinkedIn](https://www.linkedin.com/in/tzfong/)

A light Vulkan-CUDA interop pathtracer engine.

TODO
--------------------
1) **Complete Vkguide setup (O)**
a) *Consider switching to SDL*
2) **Enable `VK_KHR_external_memory_capabilities` extension, `VK_KHR_external` memory on device, and `VK_KHR_external_memory_win32` (O)**
3) **Implement quad rendering w/ image textures in Vulkan (-)**
a) *Setup Vulkan instance, physical device, surface, and logical device (O)*
b) *Setup Vulkan swapchain (O)*
b) *Setup synchronization devices, frames in flight (O)*
b) *Walk through compute shader rendering (-)*
4) Implement GLTF loading to Vulkan (we'll get a pointer to pass it to CUDA)
5) Implement multiple object rasterization in Vulkan
6) Implement CUDA pathtracing
7) Implement Vulkan interop
8) Setup CMake and port to GPU
a) *reformat project structure*

*6 can come before or after 4 and 5*

Stretch Goals
--------------------
1) Vulkan raytracing
2) More intelligent device selection
3) Implement BDPT
4) Implement ReSTIR

Code Structure
--------------------
`main.cpp` - wrapper for the engine init, run, and shutdown loop

`t_engine.h/cpp` - defines main engine functions

`t_types.h/cpp` - defines helper functions for Vulkan struct initialization

`t_pipelines.h/cpp` - abstractions for creating a pipeline layout/pipeline

`t_descriptors.h/cpp` - defines helper classes for managing descriptor pools via `DynamicDescriptorAllocator`,
creating descriptor set layouts via `DescriptorLayoutBuilder`, and updating descriptor sets via `DescriptorWriter`

`t_loader.h/cpp` - defines helpers for loading GLTF files

`t_initializers.h/cpp` - defines helpers to reduce boilerplate for `___CreateInfo` structs

`t_images.h/cpp` - defines wrappers for managing images via the `AllocatedImage` class

Attributions
--------------------
Resources used:
* **UPenn CIS 5650 (GPU Programming and Architecture) curriculum**
* **VkGuide** - The engine structure is adapted from VkGuide's excellent tutorial.
* **Vulkan Tutorial** - Used for initial VS project setup, and used for Vulkan object creation in place of `vkbootstrap`.
* **NVIDIA Docs Cuda Programming Guide** - Used to learn about Vulkan-CUDA interop
* **NVIDIA CUDA Samples Repo** - Used to learn about Vulkan-CUDA interop

External libraries used:
* `GLFW`
* `GLM`
* * `DearIMGui`
* `fastGLTF`
* `VMA`
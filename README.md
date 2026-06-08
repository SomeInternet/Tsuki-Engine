
Tsuki Engine
=====================
A light Vulkan-CUDA interop pathtracer engine

TODO
--------------------
1) **Complete Vkguide setup (O)**
a) *Consider switching to SDL*
2) **Enable `VK_KHR_external_memory_capabilities` extension, `VK_KHR_external` memory on device, and `VK_KHR_external_memory_win32` (-)**
3) Implement quad rendering w/ image textures in Vulkan
4) Implement GLTF loading to CUDA
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

`t_types.h/cpp` - defines helper functions for Vulkan struct initilization

Attributions
--------------------
Resources used:
* UPenn CIS 5650 (GPU Programming and Architecture) curriculum
* VkGuide
* Vulkan Tutorial

External libraries used:
* DearIMGui
* GLM
* tinyGLTF
* FMT
* GLFW
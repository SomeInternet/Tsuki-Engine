
Tsuki Engine
=====================
T Fong `SomeInternetBoi`  
[My Website](tzfong.com), [My LinkedIn](https://www.linkedin.com/in/tzfong/)

A light Vulkan-CUDA interop pathtracer engine, with a rasterized Vulkan viewport. 
Shaders are written in Slang.

TODO
--------------------
**DONE**
1) Basic Vulkan raster engine
2) glTF loading
3) Basic Vulkan-CUDA interop (0 copy memory sharing, binary semaphores)
4) CPU 2-level acceleration structure (BLAS, TLAS) construction
5) CUDA Ray-AABB, Ray-Triangle intersection
6) Basic CUDA AS traversal and raytracing

**TODO**
1) Naive pathtracing
2) Less copies for `TsukiEngine::draw()`
3) Improved rendering system, TLAS reconstruction
4) MIS
5) glTF extensions for dialectric materials

Stretch Goals
--------------------
1) Vulkan raytracing/pathtracing
2) More intelligent device selection
3) Implement BDPT
4) Implement 
5) Implement ReSTIR

Code Structure
--------------------
```
Tsuki Engine/
├── src/
│   ├── main.cpp
│   ├── t_bvh.h/cpp
│   ├── t_camera.h/cpp
│   ├── t_engine.h/cpp
│   ├── t_geometry.h
│   ├── t_images.h/cpp
│   ├── t_input.h/cpp
│   ├── t_loader.h/cpp
│   └── t_types.h
├── vulkan/
│   ├── t_descriptors.h/cpp
│   ├── t_images.h/cpp
│   ├── t_initializers.h/cpp
│   └── t_pipelines.h/cpp
├── cuda/
│   ├── t_cudacommon.h
│   ├── t_interop.h/cpp
│   ├── t_intersect.h/cpp
│   └── t_pathtrace.h/cpp
├── third-party/
│   └── ...
└── README.md
```

`main.cpp` - wrapper for the engine init, run, and shutdown loop

`t_bvh.h/cpp` - defines BLAS and TLAS construction. The actual `BVHNode` and `TLASNode` types are defined in `t_geometry.h`

`t_engine.h/cpp` - defines main engine functions, and manages acceleration structures for CUDA

`t_types.h/cpp` - defines helper functions for Vulkan struct initialization

`t_pipelines.h/cpp` - abstractions for creating a pipeline layout/pipeline

`t_descriptors.h/cpp` - defines helper classes for managing descriptor pools via `DynamicDescriptorAllocator`,
creating descriptor set layouts via `DescriptorLayoutBuilder`, and updating descriptor sets via `DescriptorWriter`

`t_loader.h/cpp` - defines helpers for loading glTF files

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
* **UPenn CIS 5610 (Advanced Rendering) curriculum**
* **PBRT 3e** - Pathtracer code and concepts

External libraries used:
* `GLFW`
* `GLM`
* `DearImGUI`
* `fastGLTF`
* `VMA`
* `stb`
* `Thrust`
cmake_minimum_required(VERSION 3.24)

project(TsukiEngine LANGUAGES CXX CUDA)

# ---------------------------------------------------------------------------
# Standards
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
# Vulkan SDK + CUDA toolkit are discovered via find_package.
find_package(Vulkan REQUIRED)
find_package(CUDAToolkit REQUIRED)

# Root of the user-installed header/static libraries (glm, glfw, imgui, ...).
# Override on the command line with -DTSUKI_LIBS_DIR=... if your layout differs.
set(TSUKI_LIBS_DIR
    "${CMAKE_SOURCE_DIR}/third-party"
    CACHE PATH "Root directory containing glm, glfw, imgui-docking, stb, VMA, fastgltf")

set(GLM_DIR        "${TSUKI_LIBS_DIR}/glm")
set(GLFW_DIR       "${TSUKI_LIBS_DIR}/glfw-3.4.bin.WIN64")
set(IMGUI_DIR      "${TSUKI_LIBS_DIR}/imgui-docking")
set(STB_DIR        "${TSUKI_LIBS_DIR}/stb-master")
set(VMA_DIR        "${TSUKI_LIBS_DIR}/VulkanMemoryAllocator-master")
set(FASTGLTF_DIR   "${TSUKI_LIBS_DIR}/fastgltf")
set(NFD_DIR        "${TSUKI_LIBS_DIR}/nativefiledialog-extended-master")

# ---------------------------------------------------------------------------
# ImGui (compiled from source, docking branch)
# ---------------------------------------------------------------------------
set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp")

# ---------------------------------------------------------------------------
# Native File Dialog (compiled from source; provides the "nfd" target)
# ---------------------------------------------------------------------------
add_subdirectory("${NFD_DIR}" "${CMAKE_BINARY_DIR}/nfd")

# ---------------------------------------------------------------------------
# fastgltf (compiled from source; provides the "fastgltf::fastgltf" target).
# Building it as part of our configuration keeps it in sync and avoids the
# "prebuild the .lib yourself" failure mode. Tests/examples/docs default OFF.
# ---------------------------------------------------------------------------
add_subdirectory("${FASTGLTF_DIR}" "${CMAKE_BINARY_DIR}/fastgltf")

# ---------------------------------------------------------------------------
# Engine sources
# ---------------------------------------------------------------------------
file(GLOB SRC_SOURCES    CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
file(GLOB VULKAN_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/vulkan/*.cpp")
file(GLOB CUDA_CPP       CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/cuda/*.cpp")
file(GLOB CUDA_SOURCES   CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/cuda/*.cu")
file(GLOB HEADERS        CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.h"
    "${CMAKE_SOURCE_DIR}/vulkan/*.h"
    "${CMAKE_SOURCE_DIR}/cuda/*.h")

add_executable(TsukiEngine
    ${SRC_SOURCES}
    ${VULKAN_SOURCES}
    ${CUDA_CPP}
    ${CUDA_SOURCES}
    ${HEADERS}
    ${IMGUI_SOURCES})

# ---------------------------------------------------------------------------
# Include directories
# ---------------------------------------------------------------------------
# The three local folders are all on the include path, so the existing flat
# includes (e.g. #include "t_engine.h") keep working without edits.
target_include_directories(TsukiEngine PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/cuda"
    "${CMAKE_SOURCE_DIR}/vulkan"
    "${GLM_DIR}"
    "${GLFW_DIR}/include"
    "${IMGUI_DIR}"
    "${STB_DIR}"
    "${VMA_DIR}/include"
    "${NFD_DIR}/src/include")
# fastgltf's include dir comes transitively from the fastgltf::fastgltf target.
# Vulkan::Vulkan and CUDA::cudart bring their own include dirs transitively.

# ---------------------------------------------------------------------------
# Link libraries
# ---------------------------------------------------------------------------
target_link_directories(TsukiEngine PRIVATE
    "${GLFW_DIR}/lib-vc2022")

target_link_libraries(TsukiEngine PRIVATE
    Vulkan::Vulkan
    CUDA::cudart
    glfw3
    fastgltf::fastgltf   # simdjson is linked in transitively
    nfd)

# ---------------------------------------------------------------------------
# Compile options
# ---------------------------------------------------------------------------
set_target_properties(TsukiEngine PROPERTIES
    CUDA_ARCHITECTURES "89"
    CUDA_SEPARABLE_COMPILATION ON)

target_compile_options(TsukiEngine PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/utf-8>
    $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>)

# Assets/Models/Shaders are loaded via paths relative to the project root, so
# launch the debugger from there.
set_target_properties(TsukiEngine PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Make Visual Studio show the sources grouped by their on-disk folders.
source_group(TREE "${CMAKE_SOURCE_DIR}" FILES
    ${SRC_SOURCES} ${VULKAN_SOURCES} ${CUDA_CPP} ${CUDA_SOURCES} ${HEADERS})

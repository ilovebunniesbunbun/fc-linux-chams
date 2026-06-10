cmake_minimum_required(VERSION 3.10)
project(fc2_chams_v2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Find OpenGL
find_package(OpenGL REQUIRED)

# Find GLFW3
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

# Find X11 and Xext for shape extension click-through
find_package(X11 REQUIRED)
find_library(XEXT_LIB Xext REQUIRED)

# Warning levels and vendor silencing flags
if(MSVC)
    set(PROJECT_WARNING_FLAGS /W4)
    set(VENDOR_WARNING_FLAGS /W0)
else()
    set(PROJECT_WARNING_FLAGS -Wall -Wextra)
    set(VENDOR_WARNING_FLAGS -w)
endif()

# ==========================================
# 3rd Party Target Definitions (Isolated)
# ==========================================

# 1. Meshoptimizer target
add_library(meshopt STATIC
    src/external/meshopt/Allocator.cpp
    src/external/meshopt/IndexCodec.cpp
    src/external/meshopt/IndexGenerator.cpp
    src/external/meshopt/OverdrawOptimizer.cpp
    src/external/meshopt/VcacheOptimizer.cpp
    src/external/meshopt/VertexCodec.cpp
    src/external/meshopt/VfetchOptimizer.cpp
)
target_include_directories(meshopt SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/meshopt
)
target_compile_options(meshopt PRIVATE ${VENDOR_WARNING_FLAGS})

# 2. Zstd target
add_library(zstd STATIC
    src/external/zstd/zstdimpl.cpp
)
target_include_directories(zstd SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/zstd
)
target_compile_definitions(zstd PUBLIC ZSTD_DISABLE_ASM)
target_compile_options(zstd PRIVATE ${VENDOR_WARNING_FLAGS})

# 3. ImGui target
add_library(imgui STATIC
    src/external/imgui/imgui.cpp
    src/external/imgui/imgui_draw.cpp
    src/external/imgui/imgui_tables.cpp
    src/external/imgui/imgui_widgets.cpp
    src/external/imgui/imgui_impl_glfw.cpp
    src/external/imgui/imgui_impl_opengl3.cpp
)
target_include_directories(imgui SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/imgui
)
target_include_directories(imgui PRIVATE
    ${GLFW_INCLUDE_DIRS}
    ${OPENGL_INCLUDE_DIR}
)
target_link_libraries(imgui PRIVATE
    OpenGL::GL
    ${GLFW_LIBRARIES}
)
target_compile_options(imgui PRIVATE ${VENDOR_WARNING_FLAGS})


# ==========================================
# Project Targets
# ==========================================

# Shared parser sources
set(PARSER_SOURCES
    src/vpk/vmdl/model.cpp
    src/vpk/vmdl/vmdl.cpp
    src/vpk/vmdl/maps/map_parser.cpp
)

# Main Application sources
set(APP_SOURCES
    src/main.cpp
    src/model_cache.cpp
    src/overlay_client.cpp
    src/renderer/gpu_chams.cpp
    src/renderer/depth_prepass.cpp
    src/renderer/esp_renderer.cpp
    src/config.cpp
    src/menu_client.cpp
)

# 1. Main Executable Target
add_executable(fc2_chams ${APP_SOURCES} ${PARSER_SOURCES})

target_include_directories(fc2_chams PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${OPENGL_INCLUDE_DIR}
    ${GLFW_INCLUDE_DIRS}
    ${X11_INCLUDE_DIR}
)

target_link_libraries(fc2_chams PRIVATE
    imgui
    meshopt
    zstd
    OpenGL::GL
    ${GLFW_LIBRARIES}
    ${X11_LIBRARIES}
    ${XEXT_LIB}
    rt
    pthread
    m
)

target_compile_options(fc2_chams PRIVATE 
    ${PROJECT_WARNING_FLAGS}
    $<$<CONFIG:Release>:-O3 -march=native -ffast-math -flto>
)
target_link_options(fc2_chams PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native -flto>
)

# 2. Standalone Model Load Test Target
add_executable(test_model_load scratch/test_model_load.cpp ${PARSER_SOURCES})

target_include_directories(test_model_load PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(test_model_load PRIVATE
    meshopt
    zstd
    rt
    pthread
    m
)

target_compile_options(test_model_load PRIVATE 
    ${PROJECT_WARNING_FLAGS}
    $<$<CONFIG:Release>:-O3 -march=native -ffast-math -flto>
)
target_link_options(test_model_load PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native -flto>
)

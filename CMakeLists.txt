cmake_minimum_required(VERSION 3.10)
project(fc2_chams_v2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_definitions(ZSTD_DISABLE_ASM)

# Find OpenGL
find_package(OpenGL REQUIRED)

# Find GLFW3
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

# Find X11 and Xext for shape extension click-through
find_package(X11 REQUIRED)
find_library(XEXT_LIB Xext REQUIRED)

# Shared sources for the parser & dependencies
set(PARSER_SOURCES
    src/vpk/vmdl/model.cpp
    src/vpk/vmdl/vmdl.cpp
    src/vpk/vmdl/maps/map_parser.cpp

    # Meshoptimizer sources
    src/external/meshopt/Allocator.cpp
    src/external/meshopt/IndexCodec.cpp
    src/external/meshopt/IndexGenerator.cpp
    src/external/meshopt/OverdrawOptimizer.cpp
    src/external/meshopt/VcacheOptimizer.cpp
    src/external/meshopt/VertexCodec.cpp
    src/external/meshopt/VfetchOptimizer.cpp

    # Zstd implementation wrapper (single translation unit compilation)
    src/external/zstd/zstdimpl.cpp
)

# Main Application sources
set(APP_SOURCES
    src/main.cpp
    src/model_cache.cpp
    src/overlay_client.cpp
    src/renderer/gpu_chams.cpp
    src/renderer/depth_prepass.cpp
    src/config.cpp
    src/menu_client.cpp
    src/external/imgui/imgui.cpp
    src/external/imgui/imgui_draw.cpp
    src/external/imgui/imgui_tables.cpp
    src/external/imgui/imgui_widgets.cpp
    src/external/imgui/imgui_impl_glfw.cpp
    src/external/imgui/imgui_impl_opengl3.cpp
)

# 1. Main Executable Target
add_executable(fc2_chams ${APP_SOURCES} ${PARSER_SOURCES})

target_include_directories(fc2_chams PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/meshopt
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/zstd
    ${OPENGL_INCLUDE_DIR}
    ${GLFW_INCLUDE_DIRS}
    ${X11_INCLUDE_DIR}
)

target_link_libraries(fc2_chams PRIVATE
    OpenGL::GL
    ${GLFW_LIBRARIES}
    ${X11_LIBRARIES}
    ${XEXT_LIB}
    rt
    pthread
    m
)

target_compile_options(fc2_chams PRIVATE 
    $<$<CONFIG:Release>:-O3 -march=native -ffast-math -flto>
)
target_link_options(fc2_chams PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native -flto>
)


# 2. Standalone Model Load Test Target
add_executable(test_model_load scratch/test_model_load.cpp ${PARSER_SOURCES})

target_include_directories(test_model_load PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/meshopt
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external/zstd
)

target_link_libraries(test_model_load PRIVATE
    rt
    pthread
    m
)

target_compile_options(test_model_load PRIVATE 
    $<$<CONFIG:Release>:-O3 -march=native -ffast-math -flto>
)
target_link_options(test_model_load PRIVATE
    $<$<CONFIG:Release>:-O3 -march=native -flto>
)

cmake_minimum_required(VERSION 3.10)
project(fc2_chams_v2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_definitions(GLM_ENABLE_EXPERIMENTAL)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Find OpenGL
find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm REQUIRED)
find_package(spdlog REQUIRED)

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
file(GLOB_RECURSE PARSER_SOURCES CONFIGURE_DEPENDS
    "src/vpk/*.cpp"
)

# Main Application sources
file(GLOB APP_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/app/*.cpp"
    "src/renderer/*.cpp"
    "src/menu/*.cpp"
    "src/overlay/*.cpp"
)
list(FILTER APP_SOURCES EXCLUDE REGEX "src/scratch_dump.cpp")

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
    glm::glm
    spdlog::spdlog
    OpenGL::GL
    GLEW::GLEW
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

# Copy assets to build directory
add_custom_command(TARGET fc2_chams POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:fc2_chams>/assets
    COMMENT "Copying assets to build directory"
)

option(FC2_BUILD_TESTS "Build C++ unit tests" OFF)
if(FC2_BUILD_TESTS)
    enable_testing()
    add_executable(unit_tests tests/cpp/test_main.cpp src/config.cpp ${PARSER_SOURCES})
    target_include_directories(unit_tests PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/overlay
    )
    target_link_libraries(unit_tests PRIVATE
        meshopt
        zstd
        glm::glm
        spdlog::spdlog
        rt
        pthread
        m
    )
    add_test(NAME test_matrix_inversion COMMAND unit_tests --matrix-inversion)
    add_test(NAME test_view_matrix_extrapolator COMMAND unit_tests --extrapolator)
    add_test(NAME test_shm_reader_bounds COMMAND unit_tests --shm-bounds)
    add_test(NAME test_kv3_parser COMMAND unit_tests --kv3)
    add_test(NAME test_vpk_reader COMMAND unit_tests --vpk)
    add_test(NAME test_trajectory_sim COMMAND unit_tests --trajectory-sim)
    add_test(NAME test_bvh_raycast COMMAND unit_tests --bvh-raycast)
    add_test(NAME test_color_conversions COMMAND unit_tests --color-conv)
    add_test(NAME test_config_serialize COMMAND unit_tests --config-serialize)
    add_test(NAME test_bvh_construction COMMAND unit_tests --bvh-construction)
endif()

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

cmake_minimum_required(VERSION 3.31)

project(
    MeshTools
    VERSION 0.1.0
    DESCRIPTION "Extensible mesh editor scaffold"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MESHTOOLS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(
    MESHTOOLS_USE_FETCHCONTENT
    "Download GLFW and Dear ImGui during configure when local source dirs are not provided"
    ON
)

set(MESHTOOLS_GLFW_SOURCE_DIR "" CACHE PATH "Path to a local GLFW source checkout")
set(MESHTOOLS_IMGUI_SOURCE_DIR "" CACHE PATH "Path to a local Dear ImGui source checkout")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Dependencies)

find_package(OpenGL REQUIRED)
meshtools_configure_dependencies()

add_library(meshtools_options INTERFACE)
target_compile_features(meshtools_options INTERFACE cxx_std_20)

if(APPLE)
    target_compile_definitions(meshtools_options INTERFACE GL_SILENCE_DEPRECATION)
endif()

add_library(meshtools_warnings INTERFACE)
meshtools_set_project_warnings(meshtools_warnings "${MESHTOOLS_WARNINGS_AS_ERRORS}")

add_library(meshtools_core STATIC
    src/app/EditorApplication.cpp
    src/platform/GlfwWindow.cpp
    src/ui/EditorUi.cpp
)

target_include_directories(meshtools_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(meshtools_core
    PUBLIC
        imgui
        glfw
        OpenGL::GL
        meshtools_options
        meshtools_warnings
)

add_executable(meshtools_editor src/main.cpp)
target_link_libraries(meshtools_editor PRIVATE meshtools_core)

set_target_properties(meshtools_editor PROPERTIES OUTPUT_NAME meshtools)


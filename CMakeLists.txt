cmake_minimum_required(VERSION 3.15)

project(ficsit-companion)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if (NOT DEFINED EMSCRIPTEN)
    # OpenGL
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/opengl.cmake")

    # SDL2
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdl2.cmake")
endif ()

# Imgui
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/imgui.cmake")

add_subdirectory(ficsit-companion)

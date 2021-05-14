cmake_minimum_required(VERSION 3.5)
project(wgpu-starter)

if (NOT WIN32)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

if (NOT EMSCRIPTEN)
    add_definitions(-DNOMINMAX
        -DSDL_MAIN_HANDLED
        -DWIN32_LEAN_AND_MEAN
        -DDAWN_ENABLE_BACKEND_D3D12)

    find_package(Threads REQUIRED)
    find_package(SDL2 CONFIG REQUIRED)
    find_package(Dawn REQUIRED)

    add_library(webgpu_cpp webgpu_cpp.cpp)
    target_link_libraries(webgpu_cpp PUBLIC Dawn)
else()
    set(CMAKE_EXE_LINKER_FLAGS "-s USE_WEBGPU=1")
    # Generate the index.html file that will load our Emscripten compiled module
    set(APP_TARGET_NAME wgpu-starter)
    configure_file(index.html.in ${CMAKE_CURRENT_BINARY_DIR}/index.html @ONLY)
endif()

find_package(Vulkan REQUIRED)

add_spirv_embed_library(spv_shaders triangle_vert.vert triangle_frag.frag)

add_executable(wgpu-starter main.cpp)

set_target_properties(wgpu-starter PROPERTIES
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED ON)

target_link_libraries(wgpu-starter PRIVATE spv_shaders)

if (NOT EMSCRIPTEN)
    target_link_libraries(wgpu-starter PUBLIC
        SDL2::SDL2
        SDL2::SDL2main)

    target_link_libraries(wgpu-starter
        PUBLIC
        webgpu_cpp)
endif()
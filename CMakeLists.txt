cmake_minimum_required(VERSION 3.5)
project(wgpu-starter)

if (NOT WIN32)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_LIST_DIR}/cmake")

if (NOT EMSCRIPTEN)
    add_definitions(-DNOMINMAX
        -DSDL_MAIN_HANDLED
        -DWIN32_LEAN_AND_MEAN)

    if (WIN32)
        add_definitions(-DDAWN_ENABLE_BACKEND_D3D12)
    elseif (APPLE)
        add_definitions(-DDAWN_ENABLE_BACKEND_METAL)
    endif()

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
	CXX_STANDARD 11
	CXX_STANDARD_REQUIRED ON)

target_link_libraries(wgpu-starter PRIVATE spv_shaders)

if (NOT EMSCRIPTEN)
    if (TARGET SDL2::SDL2)
        target_link_libraries(wgpu-starter PUBLIC
            SDL2::SDL2
            SDL2::SDL2main)
    else()
        target_include_directories(wgpu-starter PUBLIC
            $<BUILD_INTERFACE:${SDL2_INCLUDE_DIRS}>)
        target_link_libraries(wgpu-starter PUBLIC ${SDL2_LIBRARIES})
    endif()

    target_link_libraries(wgpu-starter
        PUBLIC
        webgpu_cpp)

    if (APPLE)
        find_library(QUARTZ_CORE QuartzCore)
        if (NOT QUARTZ_CORE)
            message(FATAL_ERROR "QuartzCore not found")
        endif()

        find_library(METAL_LIB Metal)
        if (NOT METAL_LIB)
            message(FATAL_ERROR "Metal not found")
        endif()

        find_library(COCOA_LIB Cocoa)
        if (NOT COCOA_LIB)
            message(FATAL_ERROR "Cocoa not found")
        endif()

        add_library(metal_util metal_util.mm)

        target_link_libraries(metal_util PUBLIC
            Dawn
            ${QUARTZ_CORE}
            ${METAL_LIB}
            ${COCOA_LIB})

        set_target_properties(metal_util PROPERTIES
            CXX_STANDARD 11)

        if (TARGET SDL2::SDL2)
            target_link_libraries(metal_util PUBLIC
                SDL2::SDL2)
        else()
            target_include_directories(metal_util PUBLIC
                $<BUILD_INTERFACE:${SDL2_INCLUDE_DIRS}>)
            target_link_libraries(metal_util PUBLIC ${SDL2_LIBRARIES})
        endif()

        target_link_libraries(wgpu-starter PUBLIC metal_util)
    endif()
endif()

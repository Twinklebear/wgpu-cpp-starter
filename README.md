# WebGPU C++ Starter Project

A starter code for cross-platform (i.e., web & native) C++ WebGPU projects.

## Building for Web with Emscripten

You can build the project using Emscripten's CMake wrapper. The VulkanSDK is
required to provide `glslc` to compile shaders to SPV. This path should be
to the specific version of Vulkan you have installed, e.g., `-DVULKAN_SDK=<path>/VulkanSDK/<version>`.
On macOS you must also specify the `macOS` subdirectory after the version.
The Vulkan requirement will be removed if SPV support is removed from WebGPU
and replaced with WGSL.

```
mkdir emcmake-build
emcmake cmake .. -DVULKAN_SDK=<path to root of VulkanSDK>
cmake --build .
```

You can then run a webserver in the build directory and view the application in
a browser that supports WebGPU.

```
python3 -m http.server
# navigate to localhost:8000 to see the triangle!
```

## Building for Native with Dawn (D3D12/Windows Only for Now)

The application uses [Dawn](https://dawn.googlesource.com/dawn/) to provide an
implementation of WebGPU for native platforms. Carl Woffenden has a good
[guide](https://github.com/cwoffenden/hello-webgpu/blob/master/lib/README.md)
about building Dawn which you can follow to build Dawn.
You can then build a native application with CMake, passing the location
where you build Dawn and the VulkanSDK (required to compile shaders to SPV).
SDL2 is also required to provide cross-platform windowing support, on Windows
you can install it via vcpkg and specify `-DCMAKE_TOOLCHAIN_FILE` to find the
installation.

```
mkdir cmake-build
cmake .. -DVULKAN_SDK=<path to root of VulkanSDK> `
         -DDawn_DIR=<path to Dawn build output> `
         -DCMAKE_TOOLCHAIN_FILE=<vcpkg toolchain file>
cmake --build .
```

Copy over the dll's in Dawn's build directory and you can then run the native app:

```
./<build config>/wgpu-starter.exe
```


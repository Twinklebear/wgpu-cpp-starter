# WebGPU C++ Starter Project

A starter code for cross-platform (i.e., web & native) C++ WebGPU projects.

## Building for Web with Emscripten

You can build the project using Emscripten's CMake wrapper.

```
mkdir emcmake-build
emcmake cmake ..
cmake --build .
```

You can then run a webserver in the build directory and view the application in
a browser that supports WebGPU.

```
python3 -m http.server
# navigate to localhost:8000 to see the triangle!
```

## Building for Native with Dawn

The application uses [Dawn](https://dawn.googlesource.com/dawn/) to provide an
implementation of WebGPU for native platforms. Carl Woffenden has a good
[guide](https://github.com/cwoffenden/hello-webgpu/blob/master/lib/README.md)
about building Dawn which you can follow to build Dawn.
**Note: right now this builds against Dawn at tag:** `origin/chromium/4609`.

```
mkdir cmake-build
cmake .. -DDawn_DIR=<path to Dawn build output> `
         -DCMAKE_TOOLCHAIN_FILE=<vcpkg toolchain file>
cmake --build .
```

If on Windows copy over the dll's in Dawn's build directory,
and you can then run the native app. On Mac or Linux, the dylibs/sos
just need to be in the path of the executable.

Then on Windows you can run:

```
./<build config>/wgpu-starter.exe
```

Or on Mac/Linux:

```
./wgpu-starter
```

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
#ifdef __EMSCRIPTEN__
#include <emscripten/html5_webgpu.h>
#include <webgpu/webgpu_cpp.h>
#else
#include <SDL.h>
#include <SDL_syswm.h>
#include <dawn/dawn_proc.h>
#include <dawn/webgpu_cpp.h>

#if defined(_WIN32)
#include <dawn_native/D3D12Backend.h>
#elif defined(__APPLE__)
#include <dawn_native/MetalBackend.h>
#include "metal_util.h"
#endif

#endif

#include "spv_shaders_embedded_spv.h"

#ifndef __EMSCRIPTEN__
// TODO: Vulkan for Linux, Metal for MacOS
dawn_native::Adapter request_adapter(dawn_native::Instance &instance,
                                     const wgpu::BackendType backend_type)
{
    instance.DiscoverDefaultAdapters();
    std::vector<dawn_native::Adapter> adapters = instance.GetAdapters();
    for (const auto &a : adapters) {
        wgpu::AdapterProperties props;
        a.GetProperties(&props);
        std::cout << "Adapter name: " << props.name
                  << ", driver desc: " << props.driverDescription << "\n";
        if (props.backendType == backend_type) {
            return a;
        }
    }
    throw std::runtime_error("No suitable adapter found!");
}
#endif

int main(int argc, const char **argv)
{
#ifdef __EMSCRIPTEN__
    wgpu::Instance instance;
    wgpu::Device device = wgpu::Device::Acquire(emscripten_webgpu_get_device());
#else
    dawn_native::Instance dawn_instance;

#if defined(_WIN32)
    const auto backend_type = wgpu::BackendType::D3D12;
#elif defined(__APPLE__)
    const auto backend_type = wgpu::BackendType::Metal;
#else
    // TODO linux
#endif

    auto adapter = request_adapter(dawn_instance, backend_type);
    DawnProcTable procs(dawn_native::GetProcs());
    dawnProcSetProcs(&procs);

    wgpu::Instance instance = dawn_instance.Get();
    wgpu::Device device = wgpu::Device::Acquire(adapter.CreateDevice());

    SDL_Window *window = SDL_CreateWindow(
        "wgpu-starter", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, 0);

#endif
    device.SetUncapturedErrorCallback(
        [](WGPUErrorType type, const char *msg, void *data) {
            std::cout << "WebGPU Error: " << msg << "\n" << std::flush;
            std::exit(1);
        },
        nullptr);

    wgpu::Queue queue = device.GetQueue();

#ifdef __EMSCRIPTEN__
    wgpu::SurfaceDescriptorFromCanvasHTMLSelector selector;
    selector.selector = "#webgpu-canvas";

    wgpu::SurfaceDescriptor surface_desc;
    surface_desc.nextInChain = &selector;
#else

    // Setup the swap chain for the native API
#if defined(_WIN32)
    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(window, &wm_info);

    wgpu::SurfaceDescriptorFromWindowsHWND native_surf;
    native_surf.hwnd = wm_info.info.win.window;
    native_surf.hinstance = wm_info.info.win.hinstance;
#elif defined(__APPLE__)
    auto context = metal::make_context(window);
    wgpu::SurfaceDescriptorFromMetalLayer native_surf = metal::surface_descriptor(context);
#else
#error "Linux TODO"
#endif

    wgpu::SurfaceDescriptor surface_desc;
    surface_desc.nextInChain = &native_surf;
#endif

    wgpu::Surface surface = instance.CreateSurface(&surface_desc);

    wgpu::SwapChainDescriptor swap_chain_desc;
    swap_chain_desc.format = wgpu::TextureFormat::BGRA8Unorm;
    swap_chain_desc.usage = wgpu::TextureUsage::OutputAttachment;
    swap_chain_desc.presentMode = wgpu::PresentMode::Fifo;
    swap_chain_desc.width = 640;
    swap_chain_desc.height = 480;

    wgpu::SwapChain swap_chain = device.CreateSwapChain(surface, &swap_chain_desc);

    wgpu::RenderPassColorAttachment color_attachment;
    color_attachment.view = swap_chain.GetCurrentTextureView();
    color_attachment.clearColor.r = 0.f;
    color_attachment.clearColor.g = 0.f;
    color_attachment.clearColor.b = 0.f;
    color_attachment.clearColor.a = 1.f;
    color_attachment.loadOp = wgpu::LoadOp::Clear;
    color_attachment.storeOp = wgpu::StoreOp::Store;

    wgpu::ShaderModule vertex_module;
    {
        wgpu::ShaderModuleSPIRVDescriptor shader_module_spv;
        shader_module_spv.code = triangle_vert_spv;
        shader_module_spv.codeSize = sizeof(triangle_vert_spv) / sizeof(uint32_t);

        wgpu::ShaderModuleDescriptor shader_module_desc;
        shader_module_desc.nextInChain = &shader_module_spv;
        vertex_module = device.CreateShaderModule(&shader_module_desc);
    }

    wgpu::ShaderModule fragment_module;
    {
        wgpu::ShaderModuleSPIRVDescriptor shader_module_spv;
        shader_module_spv.code = triangle_frag_spv;
        shader_module_spv.codeSize = sizeof(triangle_frag_spv) / sizeof(uint32_t);

        wgpu::ShaderModuleDescriptor shader_module_desc;
        shader_module_desc.nextInChain = &shader_module_spv;
        fragment_module = device.CreateShaderModule(&shader_module_desc);
    }

    // Upload vertex data
    const std::vector<float> vertex_data = {
        1,  -1, 0, 1,  // position
        1,  0,  0, 1,  // color
        -1, -1, 0, 1,  // position
        0,  1,  0, 1,  // color
        0,  1,  0, 1,  // position
        0,  0,  1, 1,  // color
    };
    wgpu::BufferDescriptor buffer_desc;
    buffer_desc.mappedAtCreation = true;
    buffer_desc.size = vertex_data.size() * sizeof(float);
    buffer_desc.usage = wgpu::BufferUsage::Vertex;
    wgpu::Buffer vertex_buf = device.CreateBuffer(&buffer_desc);
    std::memcpy(vertex_buf.GetMappedRange(), vertex_data.data(), buffer_desc.size);
    vertex_buf.Unmap();

    std::array<wgpu::VertexAttribute, 2> vertex_attributes;
    vertex_attributes[0].format = wgpu::VertexFormat::Float32x4;
    vertex_attributes[0].offset = 0;
    vertex_attributes[0].shaderLocation = 0;

    vertex_attributes[1].format = wgpu::VertexFormat::Float32x4;
    vertex_attributes[1].offset = 4 * 4;
    vertex_attributes[1].shaderLocation = 1;

    wgpu::VertexBufferLayout vertex_buf_layout;
    vertex_buf_layout.arrayStride = 2 * 4 * 4;
    vertex_buf_layout.attributeCount = vertex_attributes.size();
    vertex_buf_layout.attributes = vertex_attributes.data();

    wgpu::VertexState vertex_state;
    vertex_state.module = vertex_module;
    vertex_state.entryPoint = "main";
    vertex_state.bufferCount = 1;
    vertex_state.buffers = &vertex_buf_layout;

    wgpu::ColorTargetState render_target_state;
    render_target_state.format = wgpu::TextureFormat::BGRA8Unorm;

    wgpu::FragmentState fragment_state;
    fragment_state.module = fragment_module;
    fragment_state.entryPoint = "main";
    fragment_state.targetCount = 1;
    fragment_state.targets = &render_target_state;

    wgpu::RenderPipelineDescriptor2 render_pipeline_desc;
    render_pipeline_desc.vertex = vertex_state;
    render_pipeline_desc.fragment = &fragment_state;
    // Default primitive state is what we want, triangle list, no indices

    wgpu::RenderPipeline render_pipeline = device.CreateRenderPipeline2(&render_pipeline_desc);

    wgpu::RenderPassDescriptor pass_desc;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

    wgpu::RenderPassEncoder render_pass_enc = encoder.BeginRenderPass(&pass_desc);
    render_pass_enc.SetPipeline(render_pipeline);
    render_pass_enc.SetVertexBuffer(0, vertex_buf);
    render_pass_enc.Draw(3);
    render_pass_enc.EndPass();

    wgpu::CommandBuffer commands = encoder.Finish();
    // Here the # refers to the number of command buffers being submitted
    queue.Submit(1, &commands);

#ifndef __EMSCRIPTEN__
    swap_chain.Present();
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                done = true;
            }

            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                done = true;
            }
        }
    }

    SDL_DestroyWindow(window);
#endif
    return 0;
}


#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include "arcball_camera.h"
#include <glm/ext.hpp>
#include <glm/glm.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgpu.h>
#include <webgpu/webgpu_cpp.h>
#else
#include <dawn/dawn_proc.h>
#include <dawn/webgpu_cpp.h>

// Note: include order does matter on Linux
#include <SDL.h>
#include <SDL_syswm.h>

#if defined(_WIN32)
#include <dawn_native/D3D12Backend.h>
#elif defined(__APPLE__)
#include <dawn_native/MetalBackend.h>
#include "metal_util.h"
#else
#include <dawn_native/VulkanBackend.h>
#endif

#endif

const std::string WGSL_SHADER = R"(
struct VertexInput {
    [[location(0)]] position: vec4<f32>;
    [[location(1)]] color: vec4<f32>;
};

struct VertexOutput {
    [[builtin(position)]] position: vec4<f32>;
    [[location(0)]] color: vec4<f32>;
};

[[block]]
struct ViewParams {
    view_proj: mat4x4<f32>;
};

[[group(0), binding(0)]]
var<uniform> view_params: ViewParams;

[[stage(vertex)]]
fn vertex_main(vert: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    out.color = vert.color;
    out.position = view_params.view_proj * vert.position;
    return out;
};

[[stage(fragment)]]
fn fragment_main(in: VertexOutput) -> [[location(0)]] vec4<f32> {
    return vec4<f32>(in.color);
}
)";

#ifndef __EMSCRIPTEN__
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

struct AppState {
    wgpu::Device device;
    wgpu::Queue queue;

    wgpu::Surface surface;
    wgpu::SwapChain swap_chain;
    wgpu::RenderPipeline render_pipeline;
    wgpu::Buffer vertex_buf;
    wgpu::Buffer view_param_buf;
    wgpu::BindGroup bind_group;

    ArcballCamera camera;
    glm::mat4 proj;

    bool done = false;
    bool camera_changed = true;
    glm::vec2 prev_mouse = glm::vec2(-2.f);
};

int win_width = 640;
int win_height = 480;

glm::vec2 transform_mouse(glm::vec2 in)
{
    return glm::vec2(in.x * 2.f / win_width - 1.f, 1.f - 2.f * in.y / win_height);
}

void loop_iteration(void *_app_state);

int main(int argc, const char **argv)
{
    AppState *app_state = new AppState;

#ifdef __EMSCRIPTEN__
    wgpu::Instance instance;
    app_state->device = wgpu::Device::Acquire(emscripten_webgpu_get_device());
#else
    dawn_native::Instance dawn_instance;

#if defined(_WIN32)
    const auto backend_type = wgpu::BackendType::D3D12;
#elif defined(__APPLE__)
    const auto backend_type = wgpu::BackendType::Metal;
#else
    const auto backend_type = wgpu::BackendType::Vulkan;
#endif

    auto adapter = request_adapter(dawn_instance, backend_type);
    DawnProcTable procs(dawn_native::GetProcs());
    dawnProcSetProcs(&procs);

    wgpu::Instance instance = dawn_instance.Get();
    app_state->device = wgpu::Device::Acquire(adapter.CreateDevice());

    SDL_Window *window = SDL_CreateWindow("wgpu-starter",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          win_width,
                                          win_height,
                                          0);

#endif
    app_state->device.SetUncapturedErrorCallback(
        [](WGPUErrorType type, const char *msg, void *data) {
            std::cout << "WebGPU Error: " << msg << "\n" << std::flush;
            std::exit(1);
        },
        nullptr);

    app_state->queue = app_state->device.GetQueue();

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
    auto metal_context = metal::make_context(window);
    wgpu::SurfaceDescriptorFromMetalLayer native_surf =
        metal::surface_descriptor(metal_context);
#else
    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(window, &wm_info);

    // TODO Later: Maybe set up a native Vulkan swap chain instead, a bit more work but
    // may avoid possible XWayland compatibility issues since it can then run natively on
    // Wayland
    wgpu::SurfaceDescriptorFromXlib native_surf;
    native_surf.display = wm_info.info.x11.display;
    native_surf.window = wm_info.info.x11.window;
#endif

    wgpu::SurfaceDescriptor surface_desc;
    surface_desc.nextInChain = &native_surf;
#endif

    app_state->surface = instance.CreateSurface(&surface_desc);

    wgpu::SwapChainDescriptor swap_chain_desc;
    swap_chain_desc.format = wgpu::TextureFormat::BGRA8Unorm;
    swap_chain_desc.usage = wgpu::TextureUsage::RenderAttachment;
    swap_chain_desc.presentMode = wgpu::PresentMode::Fifo;
    swap_chain_desc.width = win_width;
    swap_chain_desc.height = win_height;

    app_state->swap_chain =
        app_state->device.CreateSwapChain(app_state->surface, &swap_chain_desc);

    wgpu::ShaderModule shader_module;
    {
        wgpu::ShaderModuleWGSLDescriptor shader_module_wgsl;
        shader_module_wgsl.source = WGSL_SHADER.c_str();

        wgpu::ShaderModuleDescriptor shader_module_desc;
        shader_module_desc.nextInChain = &shader_module_wgsl;
        shader_module = app_state->device.CreateShaderModule(&shader_module_desc);
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
    app_state->vertex_buf = app_state->device.CreateBuffer(&buffer_desc);
    std::memcpy(app_state->vertex_buf.GetMappedRange(), vertex_data.data(), buffer_desc.size);
    app_state->vertex_buf.Unmap();

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
    vertex_state.module = shader_module;
    vertex_state.entryPoint = "vertex_main";
    vertex_state.bufferCount = 1;
    vertex_state.buffers = &vertex_buf_layout;

    wgpu::ColorTargetState render_target_state;
    render_target_state.format = wgpu::TextureFormat::BGRA8Unorm;

    wgpu::FragmentState fragment_state;
    fragment_state.module = shader_module;
    fragment_state.entryPoint = "fragment_main";
    fragment_state.targetCount = 1;
    fragment_state.targets = &render_target_state;

    wgpu::BindGroupLayoutEntry view_param_layout_entry = {};
    view_param_layout_entry.binding = 0;
    view_param_layout_entry.buffer.hasDynamicOffset = false;
    view_param_layout_entry.buffer.type = wgpu::BufferBindingType::Uniform;
    view_param_layout_entry.visibility = wgpu::ShaderStage::Vertex;

    wgpu::BindGroupLayoutDescriptor view_params_bg_layout_desc = {};
    view_params_bg_layout_desc.entryCount = 1;
    view_params_bg_layout_desc.entries = &view_param_layout_entry;

    wgpu::BindGroupLayout view_params_bg_layout =
        app_state->device.CreateBindGroupLayout(&view_params_bg_layout_desc);

    wgpu::PipelineLayoutDescriptor pipeline_layout_desc = {};
    pipeline_layout_desc.bindGroupLayoutCount = 1;
    pipeline_layout_desc.bindGroupLayouts = &view_params_bg_layout;

    wgpu::PipelineLayout pipeline_layout =
        app_state->device.CreatePipelineLayout(&pipeline_layout_desc);

#ifndef __EMSCRIPTEN__
    wgpu::RenderPipelineDescriptor render_pipeline_desc;
#else
    // Emscripten is behind Dawn
    wgpu::RenderPipelineDescriptor2 render_pipeline_desc;
#endif
    render_pipeline_desc.vertex = vertex_state;
    render_pipeline_desc.fragment = &fragment_state;
    render_pipeline_desc.layout = pipeline_layout;
    // Default primitive state is what we want, triangle list, no indices

#ifndef __EMSCRIPTEN__
    app_state->render_pipeline = app_state->device.CreateRenderPipeline(&render_pipeline_desc);
#else
    // Emscripten is behind Dawn
    app_state->render_pipeline =
        app_state->device.CreateRenderPipeline2(&render_pipeline_desc);
#endif

    // Create the UBO for our bind group
    wgpu::BufferDescriptor ubo_buffer_desc;
    ubo_buffer_desc.mappedAtCreation = false;
    ubo_buffer_desc.size = 16 * sizeof(float);
    ubo_buffer_desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    app_state->view_param_buf = app_state->device.CreateBuffer(&ubo_buffer_desc);

    wgpu::BindGroupEntry view_param_bg_entry = {};
    view_param_bg_entry.binding = 0;
    view_param_bg_entry.buffer = app_state->view_param_buf;
    view_param_bg_entry.size = ubo_buffer_desc.size;

    wgpu::BindGroupDescriptor bind_group_desc = {};
    bind_group_desc.layout = view_params_bg_layout;
    bind_group_desc.entryCount = 1;
    bind_group_desc.entries = &view_param_bg_entry;

    app_state->bind_group = app_state->device.CreateBindGroup(&bind_group_desc);

    app_state->proj = glm::perspective(
        glm::radians(50.f), static_cast<float>(win_width) / win_height, 0.1f, 100.f);
    app_state->camera = ArcballCamera(glm::vec3(0, 0, -2.5), glm::vec3(0), glm::vec3(0, 1, 0));

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(loop_iteration, app_state, -1, 0);
#else
    while (!app_state->done) {
        loop_iteration(app_state);
    }
    SDL_DestroyWindow(window);
#endif
    return 0;
}

#ifdef __EMSCRIPTEN__
int mouse_move_callback(int type, const EmscriptenMouseEvent *event, void *_app_state)
{
    AppState *app_state = reinterpret_cast<AppState *>(_app_state);

    const glm::vec2 cur_mouse = transform_mouse(glm::vec2(event->clientX, event->clientY));

    if (app_state->prev_mouse != glm::vec2(-2.f)) {
        if (event->buttons & 1) {
            app_state->camera.rotate(app_state->prev_mouse, cur_mouse);
            app_state->camera_changed = true;
        } else if (event->buttons & 2) {
            app_state->camera.pan(cur_mouse - app_state->prev_mouse);
            app_state->camera_changed = true;
        }
    }
    app_state->prev_mouse = cur_mouse;

    return true;
}

int mouse_wheel_callback(int type, const EmscriptenWheelEvent *event, void *_app_state)
{
    AppState *app_state = reinterpret_cast<AppState *>(_app_state);

    app_state->camera.zoom(event->deltaY * 0.00005f);
    app_state->camera_changed = true;
    return true;
}
#endif

void loop_iteration(void *_app_state)
{
    AppState *app_state = reinterpret_cast<AppState *>(_app_state);
#ifndef __EMSCRIPTEN__
    SDL_Event event;
    // TODO: Because I don't make the window/canvas with SDL_CreateWindow
    // it won't attach the listeners properly to get events with SDL.
    // So I need to use the Emscripten HTML5 input API to get events instead
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            app_state->done = true;
        }
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
            app_state->done = true;
        }
        if (event.type == SDL_MOUSEMOTION) {
            const glm::vec2 cur_mouse =
                transform_mouse(glm::vec2(event.motion.x, event.motion.y));
            if (app_state->prev_mouse != glm::vec2(-2.f)) {
                if (event.motion.state & SDL_BUTTON_LMASK) {
                    app_state->camera.rotate(app_state->prev_mouse, cur_mouse);
                    app_state->camera_changed = true;
                } else if (event.motion.state & SDL_BUTTON_RMASK) {
                    app_state->camera.pan(cur_mouse - app_state->prev_mouse);
                    app_state->camera_changed = true;
                }
            }
            app_state->prev_mouse = cur_mouse;
        }
        if (event.type == SDL_MOUSEWHEEL) {
            app_state->camera.zoom(event.wheel.y * 0.05f);
            app_state->camera_changed = true;
        }
    }
#else
    emscripten_set_mousemove_callback("#webgpu-canvas", app_state, true, mouse_move_callback);
    emscripten_set_wheel_callback("#webgpu-canvas", app_state, true, mouse_wheel_callback);
#endif

    wgpu::Buffer upload_buf;
    if (app_state->camera_changed) {
        wgpu::BufferDescriptor upload_buffer_desc;
        upload_buffer_desc.mappedAtCreation = true;
        upload_buffer_desc.size = 16 * sizeof(float);
        upload_buffer_desc.usage = wgpu::BufferUsage::CopySrc;
        upload_buf = app_state->device.CreateBuffer(&upload_buffer_desc);

        const glm::mat4 proj_view = app_state->proj * app_state->camera.transform();

        std::memcpy(
            upload_buf.GetMappedRange(), glm::value_ptr(proj_view), 16 * sizeof(float));
        upload_buf.Unmap();
    }

    wgpu::RenderPassColorAttachment color_attachment;
    color_attachment.view = app_state->swap_chain.GetCurrentTextureView();
    color_attachment.clearColor.r = 0.f;
    color_attachment.clearColor.g = 0.f;
    color_attachment.clearColor.b = 0.f;
    color_attachment.clearColor.a = 1.f;
    color_attachment.loadOp = wgpu::LoadOp::Clear;
    color_attachment.storeOp = wgpu::StoreOp::Store;

    wgpu::RenderPassDescriptor pass_desc;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color_attachment;

    wgpu::CommandEncoder encoder = app_state->device.CreateCommandEncoder();
    if (app_state->camera_changed) {
        encoder.CopyBufferToBuffer(
            upload_buf, 0, app_state->view_param_buf, 0, 16 * sizeof(float));
    }

    wgpu::RenderPassEncoder render_pass_enc = encoder.BeginRenderPass(&pass_desc);
    render_pass_enc.SetPipeline(app_state->render_pipeline);
    render_pass_enc.SetVertexBuffer(0, app_state->vertex_buf);
    render_pass_enc.SetBindGroup(0, app_state->bind_group);
    render_pass_enc.Draw(3);
    render_pass_enc.EndPass();

    wgpu::CommandBuffer commands = encoder.Finish();
    // Here the # refers to the number of command buffers being submitted
    app_state->queue.Submit(1, &commands);

#ifndef __EMSCRIPTEN__
    app_state->swap_chain.Present();
#endif
    app_state->camera_changed = false;
}

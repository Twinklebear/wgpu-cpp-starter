#include "metal_util.h"
#include <stdexcept>
#include <string>
#include <Cocoa/Cocoa.h>
#include <Metal/Metal.h>
#include <QuartzCore/CAMetalLayer.h>
#include <SDL_syswm.h>

namespace metal {

struct Context {
    id<MTLDevice> device = nullptr;
    CAMetalLayer *layer = nullptr;

    Context(SDL_Window *window);
    ~Context();

    std::string device_name() const;
};

Context::Context(SDL_Window *window)
{
    // Take the first Metal device
    NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
    if (!devices) {
        throw std::runtime_error("No Metal device found!");
    }
    device = devices[0];
    [device retain];
    [devices release];

    SDL_SysWMinfo wm_info;
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(window, &wm_info);

    // Setup the Metal layer
    layer = [CAMetalLayer layer];
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;

    NSWindow *nswindow = wm_info.info.cocoa.window;
    nswindow.contentView.layer = layer;
    nswindow.contentView.wantsLayer = YES;
}

Context::~Context()
{
    [device release];
}

std::string Context::device_name() const
{
    return [device.name UTF8String];
}

std::shared_ptr<Context> make_context(SDL_Window *window)
{
    return std::make_shared<Context>(window);
}

wgpu::SurfaceDescriptorFromMetalLayer surface_descriptor(std::shared_ptr<Context> &context)
{
    wgpu::SurfaceDescriptorFromMetalLayer surf_desc;
    surf_desc.layer = context->layer;
    return surf_desc;
}
}

#include "Interpolator.hpp"

// MTLFX frame interpolation (MTLFXFrameInterpolator) is not in the public macOS SDK
// MetalFX umbrella shipped with Xcode 15.x; use blit fallback until API is available.

void Interpolator::build(uint32_t width, uint32_t height)
{
    (void)width;
    (void)height;
    m_interpolator = nullptr;
}

void Interpolator::destroy()
{
    m_interpolator = nullptr;
}

Interpolator::Interpolator(MTL::Device* device, uint32_t width, uint32_t height)
    : m_device(device), m_width(width), m_height(height)
{
    m_device->retain();
    build(width, height);
}

Interpolator::~Interpolator()
{
    destroy();
    m_device->release();
}

void Interpolator::resize(uint32_t width, uint32_t height)
{
    if (m_width == width && m_height == height) return;
    m_width  = width;
    m_height = height;
    destroy();
    build(width, height);
}

void Interpolator::interpolate(
    MTL::Texture*       prevTex,
    MTL::Texture*       currTex,
    MTL::Texture*       outTex,
    MTL::CommandBuffer* cmdBuf)
{
    (void)prevTex;

    auto* blit = cmdBuf->blitCommandEncoder();
    blit->copyFromTexture(currTex, outTex);
    blit->endEncoding();
}

#pragma once
#include <Metal/Metal.hpp>
#include <cstdint>

class Interpolator
{
public:
    Interpolator(MTL::Device* device, uint32_t width, uint32_t height);
    ~Interpolator();

    void resize(uint32_t width, uint32_t height);
    void interpolate(MTL::Texture*       prevTex,
                     MTL::Texture*       currTex,
                     MTL::Texture*       outTex,
                     MTL::CommandBuffer* cmdBuf);

private:
    void build(uint32_t width, uint32_t height);
    void destroy();

    MTL::Device* m_device       = nullptr;
    void*        m_interpolator = nullptr;
    uint32_t     m_width        = 0;
    uint32_t     m_height       = 0;
};

#pragma once
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <cstdint>
#include <memory>
#include <atomic>
#include <mutex>

class Interpolator;

class MetalEngine
{
public:
    explicit MetalEngine(MTL::Device* device);
    ~MetalEngine();

    void configure(CA::MetalLayer* layer, uint32_t width, uint32_t height);
    void resize(uint32_t width, uint32_t height);
    void submitFrame(const void* pixels, uint32_t width, uint32_t height, size_t bytesPerRow);
    void renderFrame();

private:
    void buildPipelines();
    void buildResources(uint32_t width, uint32_t height);
    void uploadToTexture(MTL::Texture* tex, const void* pixels,
                         uint32_t width, uint32_t height, size_t bytesPerRow);

    MTL::Device*        m_device   = nullptr;
    MTL::CommandQueue*  m_queue    = nullptr;
    CA::MetalLayer*     m_layer    = nullptr;

    static constexpr int kBufferCount = 2;
    MTL::Texture*        m_inputTex[kBufferCount] = {};
    int                  m_writeIdx = 0;
    int                  m_readIdx  = 1;

    MTL::RenderPipelineState* m_blitPSO = nullptr;
    std::unique_ptr<Interpolator> m_interpolator;

    uint32_t m_width  = 0;
    uint32_t m_height = 0;

    std::atomic<bool>     m_frameReady { false };
    std::mutex            m_mutex;

public:
    std::atomic<uint64_t> m_renderCount { 0 };
};

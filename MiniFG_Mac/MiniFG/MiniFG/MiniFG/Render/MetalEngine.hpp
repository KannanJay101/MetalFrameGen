#pragma once
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>

#include <cstdint>
#include <memory>
#include <atomic>
#include <mutex>
#include <cmath>
#include <dispatch/dispatch.h>

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
    void buildComputePipeline();
    void buildResources(uint32_t width, uint32_t height);
    void buildFontAtlas();
    void drawFPS(MTL::RenderCommandEncoder* enc, MTL::Texture* drawableTex);
    void uploadToTexture(MTL::Texture* tex, const void* pixels,
                         uint32_t width, uint32_t height, size_t bytesPerRow);
    double currentTime() const;

    MTL::Device*        m_device   = nullptr;
    MTL::CommandQueue*  m_queue    = nullptr;
    CA::MetalLayer*     m_layer    = nullptr;

    // Triple-buffer: prev (N-1), read (N/current), write (next capture target)
    static constexpr int kBufferCount = 3;
    MTL::Texture*        m_inputTex[kBufferCount] = {};
    int                  m_writeIdx = 0;
    int                  m_readIdx  = 1;
    int                  m_prevIdx  = 2;

    // Interpolation output texture
    MTL::Texture*        m_outputTex = nullptr;

    MTL::RenderPipelineState*  m_blitPSO    = nullptr;
    MTL::RenderPipelineState*  m_textPSO    = nullptr;
    MTL::ComputePipelineState* m_interpPSO  = nullptr;
    MTL::Texture*              m_fontAtlas  = nullptr;

    uint32_t m_width  = 0;
    uint32_t m_height = 0;

    // Frame timing for interpolation
    double m_currSubmitTime    = 0;    // when current frame was captured
    double m_prevSubmitTime    = 0;    // when previous frame was captured
    double m_presentStart      = 0;    // when we started blending prev→curr
    double m_estimatedInterval = 1.0 / 60.0; // smoothed capture interval
    bool   m_hasPrevFrame      = false; // true after ≥2 frames received

    // FPS display
    double   m_fpsTimestamp  = 0;
    uint64_t m_fpsFrameCount = 0;
    float    m_displayFPS    = 0.0f;

    std::atomic<bool>     m_frameReady { false };
    std::mutex            m_mutex;

    // In-flight frame throttle (prevents GPU queue flooding)
    static constexpr int  kMaxFramesInFlight = 1;
    dispatch_semaphore_t  m_frameSemaphore = nullptr;

    // Cached mach timebase (avoids syscall per currentTime() call)
    double                m_timebaseScale = 0;

public:
    std::atomic<uint64_t> m_renderCount { 0 };
    std::atomic<uint64_t> m_interpCount { 0 }; // frames where interpolation was used
};

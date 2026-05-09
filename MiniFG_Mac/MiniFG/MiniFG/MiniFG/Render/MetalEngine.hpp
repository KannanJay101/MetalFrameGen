#pragma once
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <CoreVideo/CoreVideo.h>

#include <cstdint>
#include <memory>
#include <atomic>
#include <mutex>
#include <cmath>
#include <dispatch/dispatch.h>

#include "../Compute/VisionOpticalFlow.hpp"

class MetalEngine
{
public:
    explicit MetalEngine(MTL::Device* device);
    ~MetalEngine();

    void configure(CA::MetalLayer* layer, uint32_t width, uint32_t height);
    void resize(uint32_t width, uint32_t height);

    // Original CPU-upload submit (kept for any caller that doesn't have a
    // CVPixelBuffer in hand). Routes through the same pixel-upload path; the
    // optical-flow worker is bypassed because Vision needs a CVPixelBuffer.
    void submitFrame(const void* pixels, uint32_t width, uint32_t height, size_t bytesPerRow);

    // Preferred submit path. Uploads pixels to the input ring AND enqueues the
    // (prev, curr) pair to the Vision optical-flow worker so synthesis can run
    // asynchronously. captureTimestamp is informational metadata.
    void submitFrame(CVPixelBufferRef buffer,
                     uint32_t width,
                     uint32_t height,
                     double captureTimestamp);

    void renderFrame();
    void setDisplayRefreshPeriod(double seconds);
    void setOpticalFlowEnabled(bool enabled);

private:
    void buildPipelines();
    void buildComputePipeline();
    void buildFlowPipelines();
    void buildResources(uint32_t width, uint32_t height);
    void resizeLocked(uint32_t width, uint32_t height);
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

    // Per-slot CVPixelBuffer + frame ID, used by the optical-flow worker to
    // correlate async results back to the (prev, curr) pair that produced them.
    CVPixelBufferRef     m_inputPixelBuffer[kBufferCount] = {};
    uint64_t             m_inputFrameID[kBufferCount] = {};
    double               m_inputFrameTimestamp[kBufferCount] = {};
    uint64_t             m_nextFrameID = 1;

    // Crossfade fallback output texture
    MTL::Texture*        m_outputTex = nullptr;

    // Optical-flow synthesis output ring (one per in-flight render frame so
    // overlapping GPU work doesn't trample the same texture).
    static constexpr int kMaxFramesInFlight = 1;
    MTL::Texture*        m_synthesizedTex[kMaxFramesInFlight] = {};
    MTL::Texture*        m_confidenceTex[kMaxFramesInFlight] = {};
    int                  m_flowOutputSlot = 0;

    MTL::RenderPipelineState*  m_blitPSO    = nullptr;
    MTL::RenderPipelineState*  m_textPSO    = nullptr;
    MTL::ComputePipelineState* m_interpPSO  = nullptr;          // existing crossfade
    MTL::ComputePipelineState* m_flowConfidencePSO = nullptr;   // NEW: confidence mask
    MTL::ComputePipelineState* m_flowSynthesisPSO  = nullptr;   // NEW: motion-aware warp
    MTL::Texture*              m_fontAtlas  = nullptr;

    std::unique_ptr<VisionOpticalFlow> m_opticalFlow;
    bool m_opticalFlowEnabled = true;

    uint32_t m_width  = 0;
    uint32_t m_height = 0;

    // Frame timing for interpolation
    double m_currSubmitTime    = 0;    // when current frame was captured
    double m_prevSubmitTime    = 0;    // when previous frame was captured
    double m_presentStart      = 0;    // when we started blending prev→curr
    double m_estimatedInterval = 1.0 / 60.0; // smoothed capture interval
    double m_displayInterval   = 0;    // display refresh period; 0 = unknown
    bool   m_hasPrevFrame      = false; // true after ≥2 frames received

    // FPS display
    double   m_fpsTimestamp  = 0;
    uint64_t m_fpsFrameCount = 0;
    float    m_displayFPS    = 0.0f;

    std::atomic<bool>     m_frameReady { false };
    std::mutex            m_mutex;

    // In-flight frame throttle (prevents GPU queue flooding)
    dispatch_semaphore_t  m_frameSemaphore = nullptr;

    // Cached mach timebase (avoids syscall per currentTime() call)
    double                m_timebaseScale = 0;

public:
    std::atomic<uint64_t> m_renderCount { 0 };
    std::atomic<uint64_t> m_interpCount { 0 };       // any synthesized frame (flow or crossfade)
    std::atomic<uint64_t> m_flowInterpCount { 0 };   // synthesized via Vision optical flow
};

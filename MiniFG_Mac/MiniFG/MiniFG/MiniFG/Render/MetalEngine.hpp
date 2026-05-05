#pragma once
#include "../Compute/VisionOpticalFlow.hpp"

#include <CoreVideo/CoreVideo.h>
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
    enum class OpticalFlowDebugMode : uint32_t
    {
        Output     = 0,
        Flow       = 1,
        Confidence = 2,
        Split      = 3,
    };

    explicit MetalEngine(MTL::Device* device);
    ~MetalEngine();

    void configure(CA::MetalLayer* layer, uint32_t width, uint32_t height);
    void resize(uint32_t width, uint32_t height);
    // Zero-copy submit: `tex` is an IOSurface-backed MTLTexture vended by
    // CVMetalTextureCache on the Swift side. `keeper` is the CVMetalTextureRef
    // (as an opaque CFTypeRef) that owns the IOSurface lifetime; the engine
    // CFRetains it for as long as the slot holds the texture.
    void submitTexture(MTL::Texture* tex,
                       void* keeper,
                       CVPixelBufferRef pixelBuffer,
                       double captureTimestamp);
    void renderFrame();
    void setDisplayRefreshPeriod(double seconds);
    void setOpticalFlowEnabled(bool enabled);
    void setOpticalFlowDebugMode(uint32_t mode);

private:
    void buildPipelines();
    void buildFlowPipelines();
    void buildResources(uint32_t width, uint32_t height);
    void resizeLocked(uint32_t width, uint32_t height);
    void buildFontAtlas();
    void drawFPS(MTL::RenderCommandEncoder* enc, MTL::Texture* drawableTex);
    double currentTime() const;

    // Single source of truth for the "are we mid-interpolation?" gate shared by
    // the early-exit in renderFrame and the interpolation dispatch. Caller holds
    // m_mutex. Returns true when 0.01 < blend < 0.99 AND display is faster than
    // capture AND we have a previous frame. Writes the clamped blend factor to
    // blendFactorOut in all cases.
    bool needsRender(float& blendFactorOut) const;

    MTL::Device*        m_device   = nullptr;
    MTL::CommandQueue*  m_queue    = nullptr;
    CA::MetalLayer*     m_layer    = nullptr;

    // Triple-buffer: prev (N-1), read (N/current), write (next capture target)
    static constexpr int kBufferCount = 3;
    MTL::Texture*        m_inputTex[kBufferCount] = {};
    // Parallel ring of CVMetalTextureRef (as opaque CFTypeRef). Each entry keeps
    // its paired m_inputTex[i]'s IOSurface alive. CFRetained on install in
    // submitTexture, CFReleased on replace/destruct.
    void*                m_inputTexKeeper[kBufferCount] = {};
    CVPixelBufferRef     m_inputPixelBuffer[kBufferCount] = {};
    uint64_t             m_inputFrameID[kBufferCount] = {};
    double               m_inputFrameTimestamp[kBufferCount] = {};
    int                  m_writeIdx = 0;
    int                  m_readIdx  = 1;
    int                  m_prevIdx  = 2;
    uint64_t             m_nextFrameID = 1;

    // In-flight frame throttle (prevents GPU queue flooding while giving the
    // display pipeline enough room to sustain high-refresh output).
    static constexpr int  kMaxFramesInFlight = 3;

    MTL::RenderPipelineState*  m_blitPSO    = nullptr;
    MTL::RenderPipelineState*  m_textPSO    = nullptr;
    MTL::RenderPipelineState*  m_interpPSO  = nullptr;
    MTL::ComputePipelineState* m_flowConfidencePSO = nullptr;
    MTL::ComputePipelineState* m_flowSynthesisPSO  = nullptr;
    MTL::ComputePipelineState* m_flowDebugPSO      = nullptr;
    MTL::ComputePipelineState* m_confidenceDebugPSO = nullptr;
    MTL::Texture*              m_fontAtlas  = nullptr;
    MTL::Texture*              m_confidenceTex[kMaxFramesInFlight] = {};
    MTL::Texture*              m_synthesizedTex[kMaxFramesInFlight] = {};
    int                        m_flowOutputSlot = 0;

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

    dispatch_semaphore_t  m_frameSemaphore = nullptr;

    // Cached mach timebase (avoids syscall per currentTime() call)
    double                m_timebaseScale = 0;
    bool                  m_opticalFlowEnabled = true;
    OpticalFlowDebugMode  m_opticalFlowDebugMode = OpticalFlowDebugMode::Output;
    std::unique_ptr<VisionOpticalFlow> m_opticalFlow;

public:
    std::atomic<uint64_t> m_renderCount { 0 };
    std::atomic<uint64_t> m_interpCount { 0 }; // frames where interpolation was used
    std::atomic<uint64_t> m_flowInterpCount { 0 };
};

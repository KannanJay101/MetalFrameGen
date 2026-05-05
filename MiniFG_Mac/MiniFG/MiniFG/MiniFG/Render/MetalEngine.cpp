#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include "MetalEngine.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <mach/mach_time.h>
#include <algorithm>
#include <CoreFoundation/CoreFoundation.h>

namespace
{
struct FlowConfidenceParams
{
    uint32_t hasReverseFlow    = 0;
    float    consistencyBias   = 0.75f;
    float    consistencyScale  = 0.10f;
    float    fallbackConfidence = 0.65f;
};

struct FlowSynthesisParams
{
    float    blendFactor    = 0.0f;
    uint32_t hasReverseFlow = 0;
    uint32_t splitScreen    = 0;
    uint32_t reserved       = 0;
};
}

MetalEngine::MetalEngine(MTL::Device* device)
    : m_device(device)
{
    m_device->retain();
    m_queue = m_device->newCommandQueue();
    assert(m_queue && "Failed to create MTLCommandQueue");
    m_opticalFlow = std::make_unique<VisionOpticalFlow>(m_device);

    m_frameSemaphore = dispatch_semaphore_create(kMaxFramesInFlight);

    // Cache mach timebase so currentTime() avoids a syscall each call
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    m_timebaseScale = (double)info.numer / (double)info.denom / 1e9;
}

MetalEngine::~MetalEngine()
{
    for (int i = 0; i < kBufferCount; ++i) {
        if (m_inputTex[i]) m_inputTex[i]->release();
        if (m_inputTexKeeper[i]) CFRelease((CFTypeRef)m_inputTexKeeper[i]);
        if (m_inputPixelBuffer[i]) CFRelease(m_inputPixelBuffer[i]);
    }
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        if (m_confidenceTex[i])  m_confidenceTex[i]->release();
        if (m_synthesizedTex[i]) m_synthesizedTex[i]->release();
    }

    if (m_blitPSO)   m_blitPSO->release();
    if (m_textPSO)   m_textPSO->release();
    if (m_interpPSO) m_interpPSO->release();
    if (m_flowConfidencePSO)  m_flowConfidencePSO->release();
    if (m_flowSynthesisPSO)   m_flowSynthesisPSO->release();
    if (m_flowDebugPSO)       m_flowDebugPSO->release();
    if (m_confidenceDebugPSO) m_confidenceDebugPSO->release();
    if (m_fontAtlas)  m_fontAtlas->release();
    m_opticalFlow.reset();
    if (m_queue)      m_queue->release();
    if (m_device)     m_device->release();
}

double MetalEngine::currentTime() const
{
    return (double)mach_absolute_time() * m_timebaseScale;
}

bool MetalEngine::needsRender(float& blendFactorOut) const
{
    blendFactorOut = 1.0f;
    // Require display ≥ ~1.33× capture rate. Rules out 60:60 jitter flapping
    // (which reads as ghosting) while still firing on ProMotion 120:60.
    bool displayFasterThanCapture =
        (m_displayInterval > 0 && m_displayInterval < m_estimatedInterval * 0.75);
    if (!m_hasPrevFrame || !displayFasterThanCapture) return false;
    double elapsed = currentTime() - m_presentStart;
    double t = elapsed / m_estimatedInterval;
    blendFactorOut = std::min(std::max((float)t, 0.0f), 1.0f);
    return (blendFactorOut > 0.01f && blendFactorOut < 0.99f);
}

void MetalEngine::configure(CA::MetalLayer* layer, uint32_t width, uint32_t height)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_layer = layer;
    buildPipelines();
    buildFlowPipelines();
    buildFontAtlas();
    buildResources(width, height);
}

void MetalEngine::resize(uint32_t width, uint32_t height)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    resizeLocked(width, height);
}

void MetalEngine::setDisplayRefreshPeriod(double seconds)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_displayInterval = seconds;
}

void MetalEngine::setOpticalFlowEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_opticalFlowEnabled = enabled;
    if (m_opticalFlow) m_opticalFlow->setEnabled(enabled);
}

void MetalEngine::setOpticalFlowDebugMode(uint32_t mode)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    switch (mode) {
        case static_cast<uint32_t>(OpticalFlowDebugMode::Flow):
            m_opticalFlowDebugMode = OpticalFlowDebugMode::Flow;
            break;
        case static_cast<uint32_t>(OpticalFlowDebugMode::Confidence):
            m_opticalFlowDebugMode = OpticalFlowDebugMode::Confidence;
            break;
        case static_cast<uint32_t>(OpticalFlowDebugMode::Split):
            m_opticalFlowDebugMode = OpticalFlowDebugMode::Split;
            break;
        default:
            m_opticalFlowDebugMode = OpticalFlowDebugMode::Output;
            break;
    }
}

void MetalEngine::resizeLocked(uint32_t width, uint32_t height)
{
    if (m_width == width && m_height == height) return;
    buildResources(width, height);
}

void MetalEngine::submitTexture(MTL::Texture* tex,
                                void* keeper,
                                CVPixelBufferRef pixelBuffer,
                                double captureTimestamp)
{
    if (!tex || !pixelBuffer) return;

    uint32_t w = (uint32_t)tex->width();
    uint32_t h = (uint32_t)tex->height();

    VisionFlowPairMetadata flowPair {};
    bool shouldEnqueueFlow = false;
    CVPixelBufferRef prevFlowPixelBuffer = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (w != m_width || h != m_height) {
            // Rebuilds only the output ring — input textures are now externally
            // vended by CVMetalTextureCache.
            resizeLocked(w, h);
        }

        // Release whatever previously occupied the write slot (both the MTLTexture
        // ref and the CVMetalTextureRef keeper that pinned its IOSurface).
        int idx = m_writeIdx;
        if (m_inputTex[idx])        m_inputTex[idx]->release();
        if (m_inputTexKeeper[idx])  CFRelease((CFTypeRef)m_inputTexKeeper[idx]);
        if (m_inputPixelBuffer[idx]) CFRelease(m_inputPixelBuffer[idx]);

        uint64_t frameID = m_nextFrameID++;
        tex->retain();
        m_inputTex[idx] = tex;
        m_inputTexKeeper[idx] = keeper ? (void*)CFRetain((CFTypeRef)keeper) : nullptr;
        m_inputPixelBuffer[idx] = (CVPixelBufferRef)CFRetain(pixelBuffer);
        m_inputFrameID[idx] = frameID;
        m_inputFrameTimestamp[idx] = captureTimestamp;

        if (m_opticalFlowEnabled &&
            m_opticalFlow &&
            m_opticalFlow->isAvailable() &&
            m_inputPixelBuffer[m_readIdx] &&
            m_inputFrameID[m_readIdx] != 0 &&
            CVPixelBufferGetWidth(m_inputPixelBuffer[m_readIdx]) == w &&
            CVPixelBufferGetHeight(m_inputPixelBuffer[m_readIdx]) == h) {
            flowPair.prevFrameID = m_inputFrameID[m_readIdx];
            flowPair.currFrameID = frameID;
            flowPair.width = w;
            flowPair.height = h;
            flowPair.prevTimestamp = m_inputFrameTimestamp[m_readIdx];
            flowPair.currTimestamp = captureTimestamp;
            prevFlowPixelBuffer = (CVPixelBufferRef)CFRetain(m_inputPixelBuffer[m_readIdx]);
            shouldEnqueueFlow = true;
        }

        double now = currentTime();
        m_prevSubmitTime = m_currSubmitTime;
        m_currSubmitTime = now;
        if (m_prevSubmitTime > 0) {
            double interval = m_currSubmitTime - m_prevSubmitTime;
            if (interval > 0.001 && interval < 0.5) {
                m_estimatedInterval = m_estimatedInterval * 0.7 + interval * 0.3;
            }
        }
        m_frameReady.store(true, std::memory_order_release);
    }

    if (shouldEnqueueFlow && m_opticalFlow) {
        m_opticalFlow->enqueueFlowPair(prevFlowPixelBuffer,
                                       pixelBuffer,
                                       flowPair);
    }
    if (prevFlowPixelBuffer) CFRelease(prevFlowPixelBuffer);
}

void MetalEngine::renderFrame()
{
    // Early-exit: skip the whole pipeline (drawable acquire, blit, FPS overlay)
    // when there's no new capture AND we're not mid-interpolation. Prevents the
    // overlay from fighting the game for GPU time on redundant ticks (e.g. 60:60
    // phase drift, paused/idle capture).
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        bool newFrame = m_frameReady.load(std::memory_order_acquire);
        float unused;
        bool midInterpolation = needsRender(unused);
        if (!newFrame && !midInterpolation) return;
    }

    // Throttle: wait until a previous frame finishes GPU work.
    // This caps in-flight frames to kMaxFramesInFlight, preventing
    // GPU queue flooding that starves the game of GPU time.
    dispatch_semaphore_wait(m_frameSemaphore, DISPATCH_TIME_FOREVER);

    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    CA::MetalDrawable* drawable = m_layer->nextDrawable();
    if (!drawable) {
        dispatch_semaphore_signal(m_frameSemaphore);
        pool->release();
        return;
    }

    MTL::CommandBuffer* cmd = m_queue->commandBuffer();

    float blendFactor = 1.0f;
    bool doInterpolate = false;
    bool wantsOpticalFlow = false;
    MTL::Texture* currentTex = nullptr;
    MTL::Texture* prevTex = nullptr;
    uint64_t prevFrameID = 0;
    uint64_t currFrameID = 0;
    uint32_t frameWidth = 0;
    uint32_t frameHeight = 0;
    OpticalFlowDebugMode flowDebugMode = OpticalFlowDebugMode::Output;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_frameReady.exchange(false, std::memory_order_acq_rel))
        {
            // Rotate triple buffer: old read → prev, old write → read, old prev → write
            int oldPrev  = m_prevIdx;
            m_prevIdx    = m_readIdx;
            m_readIdx    = m_writeIdx;
            m_writeIdx   = oldPrev;

            if (m_prevSubmitTime > 0) {
                m_hasPrevFrame = true;
            }
            m_presentStart = currentTime();
        }

        currentTex = m_inputTex[m_readIdx];
        prevTex = m_inputTex[m_prevIdx];
        prevFrameID = m_inputFrameID[m_prevIdx];
        currFrameID = m_inputFrameID[m_readIdx];
        frameWidth = m_width;
        frameHeight = m_height;
        flowDebugMode = m_opticalFlowDebugMode;

        if (currentTex) currentTex->retain();
        if (prevTex) prevTex->retain();

        // Interpolate only when display rate meaningfully exceeds capture rate
        // — needsRender requires display ≤ capture × 0.75 (i.e. display ≥
        // ~1.33× capture), which rules out 60:60 jitter while still firing on
        // 120:60. At matched rates a crossfade produces ghosting.
        if (needsRender(blendFactor) &&
            m_interpPSO && prevTex && currentTex) {
            doInterpolate = true;
            wantsOpticalFlow = m_opticalFlowEnabled &&
                               m_opticalFlow &&
                               m_opticalFlow->isEnabled() &&
                               m_opticalFlow->isAvailable() &&
                               m_flowConfidencePSO &&
                               m_flowSynthesisPSO &&
                               prevFrameID != 0 &&
                               currFrameID != 0;
        }
    }

    VisionFlowTextures flowTextures;
    MTL::Texture* displayTex = currentTex;
    MTL::Texture* outputTex = nullptr;
    MTL::Texture* confidenceTex = nullptr;
    bool useOpticalFlow = false;

    if (wantsOpticalFlow &&
        m_opticalFlow->copyLatestMatchingFlow(prevFrameID, currFrameID, flowTextures) &&
        flowTextures.valid() &&
        flowTextures.metadata.width == frameWidth &&
        flowTextures.metadata.height == frameHeight) {
        std::lock_guard<std::mutex> lock(m_mutex);
        int slot = m_flowOutputSlot;
        m_flowOutputSlot = (m_flowOutputSlot + 1) % kMaxFramesInFlight;
        outputTex = m_synthesizedTex[slot];
        confidenceTex = m_confidenceTex[slot];
        if (outputTex) outputTex->retain();
        if (confidenceTex) confidenceTex->retain();
        useOpticalFlow = outputTex && confidenceTex && flowTextures.forwardTexture;
    }

    // Update FPS counter every 0.5s
    {
        double nowSec = currentTime();
        if (m_fpsTimestamp == 0) {
            m_fpsTimestamp = nowSec;
            m_fpsFrameCount = m_renderCount.load(std::memory_order_relaxed);
        } else {
            double elapsed = nowSec - m_fpsTimestamp;
            if (elapsed >= 0.5) {
                uint64_t count = m_renderCount.load(std::memory_order_relaxed);
                m_displayFPS = (float)((double)(count - m_fpsFrameCount) / elapsed);
                m_fpsTimestamp = nowSec;
                m_fpsFrameCount = count;
            }
        }
    }

    if (useOpticalFlow) {
        auto dispatch2D = [&](MTL::ComputeCommandEncoder* enc,
                              MTL::ComputePipelineState* pso) {
            NS::UInteger w = pso->threadExecutionWidth();
            NS::UInteger h = std::max<NS::UInteger>(
                1, pso->maxTotalThreadsPerThreadgroup() / w);
            MTL::Size tg(w, h, 1);
            MTL::Size grid((frameWidth + w - 1) / w, (frameHeight + h - 1) / h, 1);
            enc->dispatchThreadgroups(grid, tg);
        };

        FlowConfidenceParams confidenceParams {};
        confidenceParams.hasReverseFlow = flowTextures.hasReverse() ? 1u : 0u;
        auto* confidenceEnc = cmd->computeCommandEncoder();
        confidenceEnc->setComputePipelineState(m_flowConfidencePSO);
        confidenceEnc->setTexture(flowTextures.forwardTexture, 0);
        confidenceEnc->setTexture(flowTextures.reverseTexture, 1);
        confidenceEnc->setTexture(confidenceTex, 2);
        confidenceEnc->setBytes(&confidenceParams, sizeof(confidenceParams), 0);
        dispatch2D(confidenceEnc, m_flowConfidencePSO);
        confidenceEnc->endEncoding();

        if (flowDebugMode == OpticalFlowDebugMode::Flow && m_flowDebugPSO) {
            auto* flowDebugEnc = cmd->computeCommandEncoder();
            flowDebugEnc->setComputePipelineState(m_flowDebugPSO);
            flowDebugEnc->setTexture(flowTextures.forwardTexture, 0);
            flowDebugEnc->setTexture(outputTex, 1);
            dispatch2D(flowDebugEnc, m_flowDebugPSO);
            flowDebugEnc->endEncoding();
        } else if (flowDebugMode == OpticalFlowDebugMode::Confidence &&
                   m_confidenceDebugPSO) {
            auto* confidenceDebugEnc = cmd->computeCommandEncoder();
            confidenceDebugEnc->setComputePipelineState(m_confidenceDebugPSO);
            confidenceDebugEnc->setTexture(confidenceTex, 0);
            confidenceDebugEnc->setTexture(outputTex, 1);
            dispatch2D(confidenceDebugEnc, m_confidenceDebugPSO);
            confidenceDebugEnc->endEncoding();
        } else {
            FlowSynthesisParams synthesisParams {};
            synthesisParams.blendFactor = blendFactor;
            synthesisParams.hasReverseFlow = flowTextures.hasReverse() ? 1u : 0u;
            synthesisParams.splitScreen =
                (flowDebugMode == OpticalFlowDebugMode::Split) ? 1u : 0u;

            auto* synthEnc = cmd->computeCommandEncoder();
            synthEnc->setComputePipelineState(m_flowSynthesisPSO);
            synthEnc->setTexture(prevTex, 0);
            synthEnc->setTexture(currentTex, 1);
            synthEnc->setTexture(flowTextures.forwardTexture, 2);
            synthEnc->setTexture(flowTextures.reverseTexture, 3);
            synthEnc->setTexture(confidenceTex, 4);
            synthEnc->setTexture(outputTex, 5);
            synthEnc->setBytes(&synthesisParams, sizeof(synthesisParams), 0);
            dispatch2D(synthEnc, m_flowSynthesisPSO);
            synthEnc->endEncoding();
        }

        displayTex = outputTex;
        m_interpCount.fetch_add(1, std::memory_order_relaxed);
        m_flowInterpCount.fetch_add(1, std::memory_order_relaxed);
    }

    // Render the selected texture. Optical-flow synthesis writes an intermediate
    // texture first; the fallback path keeps the old fragment crossfade intact.
    MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::alloc()->init();
    auto* ca = rpd->colorAttachments()->object(0);
    ca->setTexture(drawable->texture());
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor(0, 0, 0, 0));
    ca->setStoreAction(MTL::StoreActionStore);

    auto* enc = cmd->renderCommandEncoder(rpd);
    if (useOpticalFlow && displayTex) {
        enc->setRenderPipelineState(m_blitPSO);
        enc->setFragmentTexture(displayTex, 0);
        enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    } else if (doInterpolate && currentTex && prevTex) {
        enc->setRenderPipelineState(m_interpPSO);
        enc->setFragmentTexture(prevTex, 0);
        enc->setFragmentTexture(currentTex, 1);
        enc->setFragmentBytes(&blendFactor, sizeof(float), 0);
        m_interpCount.fetch_add(1, std::memory_order_relaxed);
        enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    } else if (currentTex) {
        enc->setRenderPipelineState(m_blitPSO);
        enc->setFragmentTexture(currentTex, 0);
        enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    }

    // Draw FPS overlay
    drawFPS(enc, drawable->texture());

    enc->endEncoding();
    rpd->release();

    cmd->presentDrawable(drawable);

    // Signal semaphore when GPU finishes this frame
    auto sem = m_frameSemaphore;
    cmd->addCompletedHandler([sem](MTL::CommandBuffer*) {
        dispatch_semaphore_signal(sem);
    });

    cmd->commit();

    m_renderCount.fetch_add(1, std::memory_order_relaxed);

    if (currentTex) currentTex->release();
    if (prevTex) prevTex->release();
    if (outputTex) outputTex->release();
    if (confidenceTex) confidenceTex->release();

    pool->release();
}

void MetalEngine::buildPipelines()
{
    NS::Error* err = nullptr;
    const char* src = R"MSL(
        #include <metal_stdlib>
        using namespace metal;
        struct VSOut { float4 pos [[position]]; float2 uv; };
        vertex VSOut vert(uint vid [[vertex_id]]) {
            constexpr float2 pos[3] = {float2(-1,-1),float2(3,-1),float2(-1,3)};
            constexpr float2 uvs[3] = {float2(0,1),float2(2,1),float2(0,-1)};
            return { float4(pos[vid],0,1), uvs[vid] };
        }
        fragment float4 frag(VSOut in [[stage_in]],
                             texture2d<float> tex [[texture(0)]]) {
            constexpr sampler s(filter::linear, address::clamp_to_edge);
            return tex.sample(s, in.uv);
        }
        fragment float4 blendFrag(VSOut in [[stage_in]],
                                  texture2d<float> prev [[texture(0)]],
                                  texture2d<float> curr [[texture(1)]],
                                  constant float& factor [[buffer(0)]]) {
            constexpr sampler s(filter::linear, address::clamp_to_edge);
            return mix(prev.sample(s, in.uv), curr.sample(s, in.uv), factor);
        }
    )MSL";

    auto* lib = m_device->newLibrary(
        NS::String::string(src, NS::UTF8StringEncoding), nullptr, &err);
    assert(lib && "Failed to compile passthrough shader");

    auto* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    auto* vertFn = lib->newFunction(NS::String::string("vert", NS::UTF8StringEncoding));
    auto* fragFn = lib->newFunction(NS::String::string("frag", NS::UTF8StringEncoding));
    auto* blendFragFn = lib->newFunction(NS::String::string("blendFrag", NS::UTF8StringEncoding));
    desc->setVertexFunction(vertFn);
    desc->setFragmentFunction(fragFn);
    desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);

    m_blitPSO = m_device->newRenderPipelineState(desc, &err);
    assert(m_blitPSO && "Failed to create blit PSO");

    desc->setFragmentFunction(blendFragFn);
    m_interpPSO = m_device->newRenderPipelineState(desc, &err);
    assert(m_interpPSO && "Failed to create interpolation PSO");

    vertFn->release();
    fragFn->release();
    blendFragFn->release();
    desc->release();
    lib->release();

    // --- Text rendering pipeline (alpha-blended quads) ---
    const char* textSrc = R"MSL(
        #include <metal_stdlib>
        using namespace metal;

        struct TextVertex {
            float2 position;
            float2 uv;
        };

        struct TextVSOut {
            float4 pos [[position]];
            float2 uv;
        };

        vertex TextVSOut textVert(uint vid [[vertex_id]],
                                  uint iid [[instance_id]],
                                  constant TextVertex* verts [[buffer(0)]]) {
            TextVertex v = verts[iid * 6 + vid];
            return { float4(v.position, 0, 1), v.uv };
        }

        fragment float4 textFrag(TextVSOut in [[stage_in]],
                                 texture2d<float> atlas [[texture(0)]]) {
            constexpr sampler s(filter::nearest, address::clamp_to_edge);
            float a = atlas.sample(s, in.uv).r;
            return float4(1, 1, 1, a);
        }
    )MSL";

    NS::Error* textErr = nullptr;
    auto* textLib = m_device->newLibrary(
        NS::String::string(textSrc, NS::UTF8StringEncoding), nullptr, &textErr);
    assert(textLib && "Failed to compile text shader");

    auto* textDesc = MTL::RenderPipelineDescriptor::alloc()->init();
    auto* textVertFn = textLib->newFunction(NS::String::string("textVert", NS::UTF8StringEncoding));
    auto* textFragFn = textLib->newFunction(NS::String::string("textFrag", NS::UTF8StringEncoding));
    textDesc->setVertexFunction(textVertFn);
    textDesc->setFragmentFunction(textFragFn);

    auto* ca0 = textDesc->colorAttachments()->object(0);
    ca0->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
    ca0->setBlendingEnabled(true);
    ca0->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
    ca0->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
    ca0->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
    ca0->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);

    m_textPSO = m_device->newRenderPipelineState(textDesc, &textErr);
    assert(m_textPSO && "Failed to create text PSO");

    textVertFn->release();
    textFragFn->release();
    textDesc->release();
    textLib->release();
}

void MetalEngine::buildFlowPipelines()
{
    if (m_flowConfidencePSO)  { m_flowConfidencePSO->release();  m_flowConfidencePSO = nullptr; }
    if (m_flowSynthesisPSO)   { m_flowSynthesisPSO->release();   m_flowSynthesisPSO = nullptr; }
    if (m_flowDebugPSO)       { m_flowDebugPSO->release();       m_flowDebugPSO = nullptr; }
    if (m_confidenceDebugPSO) { m_confidenceDebugPSO->release(); m_confidenceDebugPSO = nullptr; }

    NS::Error* err = nullptr;
    auto* lib = m_device->newDefaultLibrary();
    if (!lib) {
        std::fprintf(stderr, "[MetalEngine] Failed to load default Metal library for optical flow kernels\n");
        return;
    }

    auto buildPipeline = [&](const char* name, MTL::ComputePipelineState*& outPipeline) {
        auto* fn = lib->newFunction(NS::String::string(name, NS::UTF8StringEncoding));
        if (!fn) {
            std::fprintf(stderr, "[MetalEngine] Missing Metal kernel '%s'\n", name);
            return;
        }
        outPipeline = m_device->newComputePipelineState(fn, &err);
        if (!outPipeline) {
            const char* errorText =
                (err && err->localizedDescription()) ? err->localizedDescription()->utf8String()
                                                     : "unknown";
            std::fprintf(stderr,
                         "[MetalEngine] Failed to create compute pipeline '%s': %s\n",
                         name,
                         errorText);
        }
        fn->release();
    };

    buildPipeline("estimateConfidence", m_flowConfidencePSO);
    buildPipeline("synthesizeFlow", m_flowSynthesisPSO);
    buildPipeline("visualizeFlow", m_flowDebugPSO);
    buildPipeline("visualizeConfidence", m_confidenceDebugPSO);

    lib->release();
}

void MetalEngine::buildResources(uint32_t width, uint32_t height)
{
    m_width  = width;
    m_height = height;

    for (int i = 0; i < kMaxFramesInFlight; ++i) {
        if (m_confidenceTex[i]) {
            m_confidenceTex[i]->release();
            m_confidenceTex[i] = nullptr;
        }
        if (m_synthesizedTex[i]) {
            m_synthesizedTex[i]->release();
            m_synthesizedTex[i] = nullptr;
        }

        auto* confidenceDesc = MTL::TextureDescriptor::texture2DDescriptor(
            MTL::PixelFormatR16Float, width, height, false);
        confidenceDesc->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
        confidenceDesc->setStorageMode(MTL::StorageModePrivate);
        m_confidenceTex[i] = m_device->newTexture(confidenceDesc);

        auto* outputDesc = MTL::TextureDescriptor::texture2DDescriptor(
            MTL::PixelFormatBGRA8Unorm, width, height, false);
        outputDesc->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
        outputDesc->setStorageMode(MTL::StorageModePrivate);
        m_synthesizedTex[i] = m_device->newTexture(outputDesc);
    }

    // Input textures are vended externally by CVMetalTextureCache (zero-copy
    // IOSurface aliases); we no longer pre-allocate the input ring here. On a
    // resolution change, stale entries that no longer match (w,h) get dropped
    // naturally when the capture pipeline rotates fresh textures into the ring.
}

// 5x7 bitmap font for: 0123456789. FPS  (space)
// Each glyph is 5 wide x 7 tall, stored as 7 bytes (1 bit per pixel, MSB-first in top 5 bits)
static const uint8_t kGlyphData[15][7] = {
    // '0'
    {0x70,0x88,0x98,0xA8,0xC8,0x88,0x70},
    // '1'
    {0x20,0x60,0x20,0x20,0x20,0x20,0x70},
    // '2'
    {0x70,0x88,0x08,0x10,0x20,0x40,0xF8},
    // '3'
    {0x70,0x88,0x08,0x30,0x08,0x88,0x70},
    // '4'
    {0x10,0x30,0x50,0x90,0xF8,0x10,0x10},
    // '5'
    {0xF8,0x80,0xF0,0x08,0x08,0x88,0x70},
    // '6'
    {0x30,0x40,0x80,0xF0,0x88,0x88,0x70},
    // '7'
    {0xF8,0x08,0x10,0x20,0x40,0x40,0x40},
    // '8'
    {0x70,0x88,0x88,0x70,0x88,0x88,0x70},
    // '9'
    {0x70,0x88,0x88,0x78,0x08,0x10,0x60},
    // '.'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x60},
    // ' '  (space)
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    // 'F'
    {0xF8,0x80,0x80,0xF0,0x80,0x80,0x80},
    // 'P'
    {0xF0,0x88,0x88,0xF0,0x80,0x80,0x80},
    // 'S'
    {0x70,0x88,0x80,0x70,0x08,0x88,0x70},
};

// Map character to glyph index, or -1
static int glyphIndex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c == '.') return 10;
    if (c == ' ') return 11;
    if (c == 'F') return 12;
    if (c == 'P') return 13;
    if (c == 'S') return 14;
    return -1;
}

void MetalEngine::buildFontAtlas()
{
    // Atlas: 15 glyphs x 5px wide = 75px, 7px tall. Single-row strip.
    const uint32_t glyphW = 5, glyphH = 7, numGlyphs = 15;
    const uint32_t atlasW = glyphW * numGlyphs;
    const uint32_t atlasH = glyphH;
    uint8_t pixels[atlasW * atlasH];
    memset(pixels, 0, sizeof(pixels));

    for (uint32_t g = 0; g < numGlyphs; ++g) {
        for (uint32_t row = 0; row < glyphH; ++row) {
            uint8_t bits = kGlyphData[g][row];
            for (uint32_t col = 0; col < glyphW; ++col) {
                bool on = (bits >> (7 - col)) & 1;
                pixels[row * atlasW + g * glyphW + col] = on ? 255 : 0;
            }
        }
    }

    auto* td = MTL::TextureDescriptor::texture2DDescriptor(
        MTL::PixelFormatR8Unorm, atlasW, atlasH, false);
    td->setUsage(MTL::TextureUsageShaderRead);
    td->setStorageMode(MTL::StorageModeShared);
    m_fontAtlas = m_device->newTexture(td);
    assert(m_fontAtlas);

    MTL::Region region = MTL::Region::Make2D(0, 0, atlasW, atlasH);
    m_fontAtlas->replaceRegion(region, 0, pixels, atlasW);
}

struct TextVertex {
    float position[2];
    float uv[2];
};

void MetalEngine::drawFPS(MTL::RenderCommandEncoder* enc, MTL::Texture* drawableTex)
{
    if (!m_textPSO || !m_fontAtlas) return;

    // Format FPS string
    char fpsStr[16];
    snprintf(fpsStr, sizeof(fpsStr), "%.1f FPS", m_displayFPS);

    int len = (int)strlen(fpsStr);
    if (len == 0) return;

    const uint32_t glyphW = 5, glyphH = 7, numGlyphs = 15;
    float atlasW = (float)(glyphW * numGlyphs);

    float drawW = (float)drawableTex->width();
    float drawH = (float)drawableTex->height();

    // Scale: each glyph is rendered at 3x native size
    float scale = 3.0f;
    float charPxW = glyphW * scale;
    float charPxH = glyphH * scale;
    float margin = 10.0f;

    // Max vertices: 6 per quad (each char)
    int maxQuads = len;
    TextVertex verts[maxQuads * 6];
    int quadCount = 0;

    auto makeQuad = [&](float x0, float y0, float x1, float y1,
                        float u0, float v0, float u1, float v1) {
        float nx0 = (x0 / drawW) * 2.0f - 1.0f;
        float ny0 = 1.0f - (y0 / drawH) * 2.0f;
        float nx1 = (x1 / drawW) * 2.0f - 1.0f;
        float ny1 = 1.0f - (y1 / drawH) * 2.0f;

        int base = quadCount * 6;
        verts[base+0] = {{nx0, ny0}, {u0, v0}};
        verts[base+1] = {{nx1, ny0}, {u1, v0}};
        verts[base+2] = {{nx0, ny1}, {u0, v1}};
        verts[base+3] = {{nx1, ny0}, {u1, v0}};
        verts[base+4] = {{nx1, ny1}, {u1, v1}};
        verts[base+5] = {{nx0, ny1}, {u0, v1}};
        quadCount++;
    };

    float cursorX = margin + 4.0f;
    float cursorY = margin + 3.0f;

    for (int i = 0; i < len; ++i) {
        int gi = glyphIndex(fpsStr[i]);
        if (gi < 0) { cursorX += charPxW; continue; }

        float u0 = (float)(gi * glyphW) / atlasW;
        float u1 = (float)(gi * glyphW + glyphW) / atlasW;
        float v0 = 0.0f;
        float v1 = 1.0f;

        makeQuad(cursorX, cursorY, cursorX + charPxW, cursorY + charPxH,
                 u0, v0, u1, v1);
        cursorX += charPxW;
    }

    if (quadCount == 0) return;

    enc->setRenderPipelineState(m_textPSO);
    enc->setFragmentTexture(m_fontAtlas, 0);
    enc->setVertexBytes(verts, sizeof(TextVertex) * quadCount * 6, 0);
    enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(quadCount * 6));
}

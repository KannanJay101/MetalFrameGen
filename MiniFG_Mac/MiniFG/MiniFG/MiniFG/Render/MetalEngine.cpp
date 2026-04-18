#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include "MetalEngine.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <mach/mach_time.h>
#include <algorithm>

MetalEngine::MetalEngine(MTL::Device* device)
    : m_device(device)
{
    m_device->retain();
    m_queue = m_device->newCommandQueue();
    assert(m_queue && "Failed to create MTLCommandQueue");

    m_frameSemaphore = dispatch_semaphore_create(kMaxFramesInFlight);

    // Cache mach timebase so currentTime() avoids a syscall each call
    mach_timebase_info_data_t info;
    mach_timebase_info(&info);
    m_timebaseScale = (double)info.numer / (double)info.denom / 1e9;
}

MetalEngine::~MetalEngine()
{
    for (int i = 0; i < kBufferCount; ++i)
        if (m_inputTex[i]) m_inputTex[i]->release();

    if (m_outputTex) m_outputTex->release();
    if (m_blitPSO)   m_blitPSO->release();
    if (m_textPSO)   m_textPSO->release();
    if (m_interpPSO) m_interpPSO->release();
    if (m_fontAtlas)  m_fontAtlas->release();
    if (m_queue)      m_queue->release();
    if (m_device)     m_device->release();
}

double MetalEngine::currentTime() const
{
    return (double)mach_absolute_time() * m_timebaseScale;
}

void MetalEngine::configure(CA::MetalLayer* layer, uint32_t width, uint32_t height)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_layer = layer;
    buildPipelines();
    buildComputePipeline();
    buildFontAtlas();
    buildResources(width, height);
}

void MetalEngine::resize(uint32_t width, uint32_t height)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    resizeLocked(width, height);
}

void MetalEngine::resizeLocked(uint32_t width, uint32_t height)
{
    if (m_width == width && m_height == height) return;
    buildResources(width, height);
}

void MetalEngine::submitFrame(const void* pixels, uint32_t width, uint32_t height,
                               size_t bytesPerRow)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (width != m_width || height != m_height) {
        resizeLocked(width, height);
    }

    MTL::Texture* writeTex = m_inputTex[m_writeIdx];
    if (!writeTex) {
        return;
    }

    writeTex->retain();
    uploadToTexture(writeTex, pixels, width, height, bytesPerRow);
    writeTex->release();

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

void MetalEngine::renderFrame()
{
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
    MTL::Texture* currentTex = nullptr;
    MTL::Texture* prevTex = nullptr;
    MTL::Texture* outputTex = nullptr;

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
        outputTex = m_outputTex;

        if (currentTex) currentTex->retain();
        if (prevTex) prevTex->retain();
        if (outputTex) outputTex->retain();

        if (m_hasPrevFrame && m_interpPSO && outputTex && prevTex && currentTex) {
            double now = currentTime();
            double elapsed = now - m_presentStart;
            double t = elapsed / m_estimatedInterval;
            blendFactor = std::min(std::max((float)t, 0.0f), 1.0f);
            doInterpolate = (blendFactor > 0.01f && blendFactor < 0.99f);
        }
    }

    MTL::Texture* displayTex = currentTex;

    if (doInterpolate) {
        // Dispatch temporal blend compute shader: mix(prev, curr, factor) → outputTex
        auto* compEnc = cmd->computeCommandEncoder();
        compEnc->setComputePipelineState(m_interpPSO);
        compEnc->setTexture(prevTex, 0);
        compEnc->setTexture(currentTex, 1);
        compEnc->setTexture(outputTex, 2);
        compEnc->setBytes(&blendFactor, sizeof(float), 0);

        MTL::Size gridSize((m_width + 15) / 16, (m_height + 15) / 16, 1);
        MTL::Size tgSize(16, 16, 1);
        compEnc->dispatchThreadgroups(gridSize, tgSize);
        compEnc->endEncoding();

        displayTex = outputTex;
        m_interpCount.fetch_add(1, std::memory_order_relaxed);
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

    // Render the display texture (either interpolated output or current capture)
    MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::alloc()->init();
    auto* ca = rpd->colorAttachments()->object(0);
    ca->setTexture(drawable->texture());
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor(0, 0, 0, 0));
    ca->setStoreAction(MTL::StoreActionStore);

    auto* enc = cmd->renderCommandEncoder(rpd);
    enc->setRenderPipelineState(m_blitPSO);
    enc->setFragmentTexture(displayTex, 0);
    enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));

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
    )MSL";

    auto* lib = m_device->newLibrary(
        NS::String::string(src, NS::UTF8StringEncoding), nullptr, &err);
    assert(lib && "Failed to compile passthrough shader");

    auto* desc = MTL::RenderPipelineDescriptor::alloc()->init();
    auto* vertFn = lib->newFunction(NS::String::string("vert", NS::UTF8StringEncoding));
    auto* fragFn = lib->newFunction(NS::String::string("frag", NS::UTF8StringEncoding));
    desc->setVertexFunction(vertFn);
    desc->setFragmentFunction(fragFn);
    desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);

    m_blitPSO = m_device->newRenderPipelineState(desc, &err);
    assert(m_blitPSO && "Failed to create blit PSO");

    vertFn->release();
    fragFn->release();
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

void MetalEngine::buildComputePipeline()
{
    NS::Error* err = nullptr;
    const char* src = R"MSL(
        #include <metal_stdlib>
        using namespace metal;

        kernel void temporalBlend(
            texture2d<float, access::read>  prev   [[texture(0)]],
            texture2d<float, access::read>  curr   [[texture(1)]],
            texture2d<float, access::write> output [[texture(2)]],
            constant float& factor                  [[buffer(0)]],
            uint2 gid [[thread_position_in_grid]])
        {
            if (gid.x >= output.get_width() || gid.y >= output.get_height()) return;
            float4 p = prev.read(gid);
            float4 c = curr.read(gid);
            output.write(mix(p, c, factor), gid);
        }
    )MSL";

    auto* lib = m_device->newLibrary(
        NS::String::string(src, NS::UTF8StringEncoding), nullptr, &err);
    assert(lib && "Failed to compile interpolation compute shader");

    auto* fn = lib->newFunction(NS::String::string("temporalBlend", NS::UTF8StringEncoding));
    m_interpPSO = m_device->newComputePipelineState(fn, &err);
    assert(m_interpPSO && "Failed to create interpolation compute PSO");

    fn->release();
    lib->release();
}

void MetalEngine::buildResources(uint32_t width, uint32_t height)
{
    m_width  = width;
    m_height = height;

    for (int i = 0; i < kBufferCount; ++i)
    {
        if (m_inputTex[i]) m_inputTex[i]->release();

        auto* td = MTL::TextureDescriptor::texture2DDescriptor(
            MTL::PixelFormatBGRA8Unorm, width, height, false);
        td->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
        td->setStorageMode(MTL::StorageModeShared);
        m_inputTex[i] = m_device->newTexture(td);
        assert(m_inputTex[i]);
    }

    // Interpolation output texture
    if (m_outputTex) m_outputTex->release();
    {
        auto* td = MTL::TextureDescriptor::texture2DDescriptor(
            MTL::PixelFormatBGRA8Unorm, width, height, false);
        td->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite);
        td->setStorageMode(MTL::StorageModePrivate);
        m_outputTex = m_device->newTexture(td);
        assert(m_outputTex);
    }
}

void MetalEngine::uploadToTexture(MTL::Texture* tex, const void* pixels,
                                   uint32_t width, uint32_t height, size_t bytesPerRow)
{
    MTL::Region region = MTL::Region::Make2D(0, 0, width, height);
    tex->replaceRegion(region, 0, pixels, bytesPerRow);
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

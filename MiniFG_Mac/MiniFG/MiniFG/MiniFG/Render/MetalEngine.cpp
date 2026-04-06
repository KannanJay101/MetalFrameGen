#define NS_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION

#include "MetalEngine.hpp"
#include "../Compute/Interpolator.hpp"
#include <cassert>
#include <cstring>

MetalEngine::MetalEngine(MTL::Device* device)
    : m_device(device)
{
    m_device->retain();
    m_queue = m_device->newCommandQueue();
    assert(m_queue && "Failed to create MTLCommandQueue");
}

MetalEngine::~MetalEngine()
{
    for (int i = 0; i < kBufferCount; ++i)
        if (m_inputTex[i]) m_inputTex[i]->release();

    if (m_blitPSO) m_blitPSO->release();
    if (m_queue)   m_queue->release();
    if (m_device)  m_device->release();
}

void MetalEngine::configure(CA::MetalLayer* layer, uint32_t width, uint32_t height)
{
    m_layer = layer;
    buildPipelines();
    buildResources(width, height);
    m_interpolator = std::make_unique<Interpolator>(m_device, width, height);
}

void MetalEngine::resize(uint32_t width, uint32_t height)
{
    if (m_width == width && m_height == height) return;
    buildResources(width, height);
    if (m_interpolator)
        m_interpolator->resize(width, height);
}

void MetalEngine::submitFrame(const void* pixels, uint32_t width, uint32_t height,
                               size_t bytesPerRow)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (width != m_width || height != m_height)
        resize(width, height);

    uploadToTexture(m_inputTex[m_writeIdx], pixels, width, height, bytesPerRow);
    m_frameReady.store(true, std::memory_order_release);
}

void MetalEngine::renderFrame()
{
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

    CA::MetalDrawable* drawable = m_layer->nextDrawable();
    if (!drawable) { pool->release(); return; }

    MTL::CommandBuffer* cmd = m_queue->commandBuffer();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_frameReady.exchange(false, std::memory_order_acq_rel))
        {
            std::swap(m_writeIdx, m_readIdx);
        }
    }

    // Always use the shader blit — handles any input/output size mismatch
    // via bilinear sampling on the fullscreen triangle.
    MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::alloc()->init();
    auto* ca = rpd->colorAttachments()->object(0);
    ca->setTexture(drawable->texture());
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor(0, 0, 0, 0));
    ca->setStoreAction(MTL::StoreActionStore);

    auto* enc = cmd->renderCommandEncoder(rpd);
    enc->setRenderPipelineState(m_blitPSO);
    enc->setFragmentTexture(m_inputTex[m_readIdx], 0);
    enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    enc->endEncoding();
    rpd->release();

    cmd->presentDrawable(drawable);
    cmd->commit();

    m_renderCount.fetch_add(1, std::memory_order_relaxed);

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
    desc->setVertexFunction(lib->newFunction(
        NS::String::string("vert", NS::UTF8StringEncoding)));
    desc->setFragmentFunction(lib->newFunction(
        NS::String::string("frag", NS::UTF8StringEncoding)));
    desc->colorAttachments()->object(0)->setPixelFormat(MTL::PixelFormatBGRA8Unorm);

    m_blitPSO = m_device->newRenderPipelineState(desc, &err);
    assert(m_blitPSO && "Failed to create blit PSO");

    desc->release();
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
        td->release();
    }
}

void MetalEngine::uploadToTexture(MTL::Texture* tex, const void* pixels,
                                   uint32_t width, uint32_t height, size_t bytesPerRow)
{
    MTL::Region region = MTL::Region::Make2D(0, 0, width, height);
    tex->replaceRegion(region, 0, pixels, bytesPerRow);
}

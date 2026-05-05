#import "MetalEngineObjC.h"
#import "MetalEngine.hpp"

@implementation MetalEngineObjC {
    MetalEngine* _engine;
}

- (instancetype)initWithDevice:(id<MTLDevice>)device {
    self = [super init];
    if (self) {
        _engine = new MetalEngine((__bridge MTL::Device*)device);
    }
    return self;
}

- (void)dealloc {
    delete _engine;
}

- (void)configureWithLayer:(CAMetalLayer*)layer {
    uint32_t w = (uint32_t)(layer.bounds.size.width  * layer.contentsScale);
    uint32_t h = (uint32_t)(layer.bounds.size.height * layer.contentsScale);
    _engine->configure((__bridge CA::MetalLayer*)layer, w, h);
}

- (void)resizeWidth:(uint32_t)width height:(uint32_t)height {
    _engine->resize(width, height);
}

- (void)submitTexture:(id<MTLTexture>)tex
               keeper:(nullable CVMetalTextureRef)keeper
          pixelBuffer:(CVPixelBufferRef)pixelBuffer
     captureTimestamp:(double)captureTimestamp {
    _engine->submitTexture((__bridge MTL::Texture*)tex,
                           (void*)keeper,
                           pixelBuffer,
                           captureTimestamp);
}

- (void)renderFrame {
    _engine->renderFrame();
}

- (void)setDisplayRefreshPeriod:(double)seconds {
    _engine->setDisplayRefreshPeriod(seconds);
}

- (void)setOpticalFlowEnabled:(BOOL)enabled {
    _engine->setOpticalFlowEnabled(enabled);
}

- (void)setOpticalFlowDebugMode:(uint32_t)mode {
    _engine->setOpticalFlowDebugMode(mode);
}

- (uint64_t)renderCount {
    return _engine->m_renderCount.load(std::memory_order_relaxed);
}

- (uint64_t)interpCount {
    return _engine->m_interpCount.load(std::memory_order_relaxed);
}

- (uint64_t)flowInterpCount {
    return _engine->m_flowInterpCount.load(std::memory_order_relaxed);
}

@end

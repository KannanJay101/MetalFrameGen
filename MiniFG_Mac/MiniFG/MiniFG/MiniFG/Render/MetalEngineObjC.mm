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

- (void)submitFrameWithPixels:(const void*)pixels
                        width:(uint32_t)width
                       height:(uint32_t)height
                  bytesPerRow:(size_t)bytesPerRow {
    _engine->submitFrame(pixels, width, height, bytesPerRow);
}

- (void)renderFrame {
    _engine->renderFrame();
}

- (uint64_t)renderCount {
    return _engine->m_renderCount.load(std::memory_order_relaxed);
}

- (uint64_t)interpCount {
    return _engine->m_interpCount.load(std::memory_order_relaxed);
}

@end

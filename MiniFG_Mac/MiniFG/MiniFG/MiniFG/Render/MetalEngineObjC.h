#pragma once
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreVideo/CoreVideo.h>

NS_ASSUME_NONNULL_BEGIN

@interface MetalEngineObjC : NSObject

- (instancetype)initWithDevice:(id<MTLDevice>)device;
- (void)configureWithLayer:(CAMetalLayer*)layer;
- (void)resizeWidth:(uint32_t)width height:(uint32_t)height;

// CPU-upload submit. Bypasses optical-flow worker (Vision needs a CVPixelBuffer).
- (void)submitFrameWithPixels:(const void*)pixels
                        width:(uint32_t)width
                       height:(uint32_t)height
                  bytesPerRow:(size_t)bytesPerRow;

// Preferred submit path. Uploads pixels to the input ring AND enqueues a
// flow pair to the Vision worker.
- (void)submitFrameWithPixelBuffer:(CVPixelBufferRef)pixelBuffer
                             width:(uint32_t)width
                            height:(uint32_t)height
                  captureTimestamp:(double)captureTimestamp;

- (void)renderFrame;
- (void)setDisplayRefreshPeriod:(double)seconds;
- (void)setOpticalFlowEnabled:(BOOL)enabled;
- (uint64_t)renderCount;
- (uint64_t)interpCount;
- (uint64_t)flowInterpCount;

@end

NS_ASSUME_NONNULL_END

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
// Zero-copy submit. `tex` aliases an IOSurface vended by CVMetalTextureCache.
// `keeper` is the CVMetalTexture that owns the IOSurface; pass nil only if
// the caller is managing IOSurface lifetime some other way.
- (void)submitTexture:(id<MTLTexture>)tex
               keeper:(nullable CVMetalTextureRef)keeper
          pixelBuffer:(CVPixelBufferRef)pixelBuffer
     captureTimestamp:(double)captureTimestamp;
- (void)renderFrame;
- (void)setDisplayRefreshPeriod:(double)seconds;
- (void)setOpticalFlowEnabled:(BOOL)enabled;
- (void)setOpticalFlowDebugMode:(uint32_t)mode;
- (uint64_t)renderCount;
- (uint64_t)interpCount;
- (uint64_t)flowInterpCount;

@end

NS_ASSUME_NONNULL_END

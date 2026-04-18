#pragma once
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

NS_ASSUME_NONNULL_BEGIN

@interface MetalEngineObjC : NSObject

- (instancetype)initWithDevice:(id<MTLDevice>)device;
- (void)configureWithLayer:(CAMetalLayer*)layer;
- (void)resizeWidth:(uint32_t)width height:(uint32_t)height;
- (void)submitFrameWithPixels:(const void*)pixels
                        width:(uint32_t)width
                       height:(uint32_t)height
                  bytesPerRow:(size_t)bytesPerRow;
- (void)renderFrame;
- (uint64_t)renderCount;
- (uint64_t)interpCount;

@end

NS_ASSUME_NONNULL_END

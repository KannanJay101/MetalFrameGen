#include "VisionOpticalFlow.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <Vision/Vision.h>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <utility>

namespace
{
static NSUInteger chooseOpticalFlowRevision()
{
    NSIndexSet* supported = VNGenerateOpticalFlowRequest.supportedRevisions;
    if (supported.count == 0) return 0;
    return supported.lastIndex;
}

static MTL::PixelFormat pixelFormatForFlowBuffer(CVPixelBufferRef pixelBuffer)
{
    switch (CVPixelBufferGetPixelFormatType(pixelBuffer)) {
        case kCVPixelFormatType_TwoComponent32Float:
            return MTL::PixelFormatRG32Float;
        case kCVPixelFormatType_TwoComponent16Half:
            return MTL::PixelFormatRG16Float;
        default:
            return MTL::PixelFormatInvalid;
    }
}

static bool shouldLogDrop(uint64_t count)
{
    return count <= 5 || (count % 60) == 0;
}

static void logDrop(const char* reason,
                    const VisionFlowPairMetadata& metadata,
                    uint64_t count)
{
    if (!shouldLogDrop(count)) return;
    std::fprintf(stderr,
                 "[VisionOpticalFlow] Dropped %s pair prev=%llu curr=%llu (%ux%u) [drop=%llu]\n",
                 reason,
                 static_cast<unsigned long long>(metadata.prevFrameID),
                 static_cast<unsigned long long>(metadata.currFrameID),
                 metadata.width,
                 metadata.height,
                 static_cast<unsigned long long>(count));
}

static bool makeFlowTexture(CVMetalTextureCacheRef textureCache,
                            CVPixelBufferRef pixelBuffer,
                            MTL::Texture*& outTexture,
                            void*& outKeeper)
{
    outTexture = nullptr;
    outKeeper  = nullptr;

    if (!textureCache || !pixelBuffer) return false;

    MTL::PixelFormat pixelFormat = pixelFormatForFlowBuffer(pixelBuffer);
    if (pixelFormat == MTL::PixelFormatInvalid) return false;

    CVMetalTextureRef cvTexture = nullptr;
    CVReturn status = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault,
        textureCache,
        pixelBuffer,
        nullptr,
        MTLPixelFormat(pixelFormat),
        CVPixelBufferGetWidth(pixelBuffer),
        CVPixelBufferGetHeight(pixelBuffer),
        0,
        &cvTexture);
    if (status != kCVReturnSuccess || !cvTexture) {
        return false;
    }

    id<MTLTexture> texture = CVMetalTextureGetTexture(cvTexture);
    if (!texture) {
        CFRelease(cvTexture);
        return false;
    }

    auto* metalTexture = (__bridge MTL::Texture*)texture;
    metalTexture->retain();
    outTexture = metalTexture;
    outKeeper = cvTexture;
    return true;
}

// Vision reports vectors for pixels in the targeted image that transform it
// into the handler image. To compute source -> destination flow, target the
// source buffer and perform the request against the destination buffer.
static CVPixelBufferRef generateDirectionalFlow(CVPixelBufferRef sourcePixelBuffer,
                                                CVPixelBufferRef destinationPixelBuffer,
                                                NSUInteger revision,
                                                NSError** outError)
{
    VNGenerateOpticalFlowRequest* request =
        [[VNGenerateOpticalFlowRequest alloc] initWithTargetedCVPixelBuffer:sourcePixelBuffer
                                                                    options:@{}];
    request.revision = revision;
    request.computationAccuracy = VNGenerateOpticalFlowRequestComputationAccuracyHigh;
    request.outputPixelFormat = kCVPixelFormatType_TwoComponent32Float;
    request.preferBackgroundProcessing = YES;

    VNImageRequestHandler* handler =
        [[VNImageRequestHandler alloc] initWithCVPixelBuffer:destinationPixelBuffer options:@{}];

    if (![handler performRequests:@[ request ] error:outError]) {
        return nullptr;
    }

    VNPixelBufferObservation* observation = request.results.firstObject;
    if (!observation) return nullptr;

    CVPixelBufferRef result = observation.pixelBuffer;
    if (result) CFRetain(result);
    return result;
}
}

struct VisionOpticalFlow::Impl
{
    explicit Impl(MTL::Device* device)
    {
        metalDevice = (__bridge id<MTLDevice>)device;
        revision = chooseOpticalFlowRevision();
        if (revision == 0) {
            std::fprintf(stderr, "[VisionOpticalFlow] No supported VNGenerateOpticalFlowRequest revisions\n");
            available.store(false, std::memory_order_release);
            return;
        }

        CVReturn status = CVMetalTextureCacheCreate(
            kCFAllocatorDefault,
            nullptr,
            metalDevice,
            nullptr,
            &textureCache);
        if (status != kCVReturnSuccess || !textureCache) {
            std::fprintf(stderr,
                         "[VisionOpticalFlow] CVMetalTextureCacheCreate failed: %d\n",
                         status);
            available.store(false, std::memory_order_release);
            return;
        }

        dispatch_queue_attr_t attr = dispatch_queue_attr_make_with_qos_class(
            DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0);
        workerQueue = dispatch_queue_create("com.minifg.opticalflow", attr);

        std::fprintf(stderr,
                     "[VisionOpticalFlow] Enabled Vision optical flow revision %u\n",
                     static_cast<unsigned>(revision));
    }

    ~Impl()
    {
        enabled.store(false, std::memory_order_release);
        available.store(false, std::memory_order_release);

        // Async Vision work may still be holding pixel buffers and using the
        // texture cache. Drain the serial queue before releasing either.
        if (workerQueue) {
            dispatch_sync(workerQueue, ^{});
        }

        {
            std::lock_guard<std::mutex> lock(latestMutex);
            latestTextures.reset();
        }

        if (textureCache) {
            CFRelease(textureCache);
            textureCache = nullptr;
        }
#if !OS_OBJECT_USE_OBJC
        if (workerQueue) dispatch_release(workerQueue);
#endif
        workerQueue = nullptr;
        metalDevice = nil;
    }

    void setEnabled(bool newEnabled)
    {
        enabled.store(newEnabled, std::memory_order_release);
    }

    bool isEnabled() const
    {
        return enabled.load(std::memory_order_acquire);
    }

    bool isAvailable() const
    {
        return available.load(std::memory_order_acquire);
    }

    bool copyLatestMatchingFlow(uint64_t prevFrameID,
                                uint64_t currFrameID,
                                VisionFlowTextures& outTextures) const
    {
        if (!isEnabled() || !isAvailable()) return false;

        std::lock_guard<std::mutex> lock(latestMutex);
        if (!latestTextures.valid()) return false;
        if (latestTextures.metadata.prevFrameID != prevFrameID ||
            latestTextures.metadata.currFrameID != currFrameID) {
            return false;
        }

        outTextures = latestTextures;
        return outTextures.valid();
    }

    void enqueueFlowPair(CVPixelBufferRef prevPixelBuffer,
                         CVPixelBufferRef currPixelBuffer,
                         const VisionFlowPairMetadata& metadata)
    {
        if (!isEnabled() || !isAvailable()) return;
        if (!workerQueue) return;
        if (!prevPixelBuffer || !currPixelBuffer) return;
        if (metadata.prevFrameID == 0 || metadata.currFrameID == 0) return;
        if (metadata.width == 0 || metadata.height == 0) return;

        latestRequestedCurrFrameID.store(metadata.currFrameID, std::memory_order_release);

        CFRetain(prevPixelBuffer);
        CFRetain(currPixelBuffer);

        dispatch_async(workerQueue, ^{
            @autoreleasepool {
                auto releaseBuffers = ^{
                    CFRelease(prevPixelBuffer);
                    CFRelease(currPixelBuffer);
                };

                if (!enabled.load(std::memory_order_acquire) ||
                    !available.load(std::memory_order_acquire)) {
                    releaseBuffers();
                    return;
                }

                if (latestRequestedCurrFrameID.load(std::memory_order_acquire) != metadata.currFrameID) {
                    uint64_t count = droppedResults.fetch_add(1, std::memory_order_relaxed) + 1;
                    logDrop("stale queued", metadata, count);
                    releaseBuffers();
                    return;
                }

                NSError* forwardError = nil;
                CVPixelBufferRef forwardBuffer = generateDirectionalFlow(
                    prevPixelBuffer, currPixelBuffer, revision, &forwardError);
                if (!forwardBuffer) {
                    uint64_t count = droppedResults.fetch_add(1, std::memory_order_relaxed) + 1;
                    if (shouldLogDrop(count)) {
                        std::fprintf(stderr,
                                     "[VisionOpticalFlow] Forward flow failed for prev=%llu curr=%llu: %s\n",
                                     static_cast<unsigned long long>(metadata.prevFrameID),
                                     static_cast<unsigned long long>(metadata.currFrameID),
                                     forwardError.localizedDescription.UTF8String ?: "unknown");
                    }
                    releaseBuffers();
                    return;
                }

                CVPixelBufferRef reverseBuffer = nullptr;
                NSError* reverseError = nil;
                reverseBuffer = generateDirectionalFlow(
                    currPixelBuffer, prevPixelBuffer, revision, &reverseError);
                if (!reverseBuffer && reverseError) {
                    uint64_t count = droppedResults.load(std::memory_order_relaxed) + 1;
                    if (shouldLogDrop(count)) {
                        std::fprintf(stderr,
                                     "[VisionOpticalFlow] Reverse flow unavailable for prev=%llu curr=%llu: %s\n",
                                     static_cast<unsigned long long>(metadata.prevFrameID),
                                     static_cast<unsigned long long>(metadata.currFrameID),
                                     reverseError.localizedDescription.UTF8String ?: "unknown");
                    }
                }

                if (latestRequestedCurrFrameID.load(std::memory_order_acquire) != metadata.currFrameID) {
                    uint64_t count = droppedResults.fetch_add(1, std::memory_order_relaxed) + 1;
                    logDrop("stale result", metadata, count);
                    if (forwardBuffer) CFRelease(forwardBuffer);
                    if (reverseBuffer) CFRelease(reverseBuffer);
                    releaseBuffers();
                    return;
                }

                if (CVPixelBufferGetWidth(forwardBuffer) != metadata.width ||
                    CVPixelBufferGetHeight(forwardBuffer) != metadata.height) {
                    uint64_t count = droppedResults.fetch_add(1, std::memory_order_relaxed) + 1;
                    logDrop("mismatched dimensions", metadata, count);
                    if (forwardBuffer) CFRelease(forwardBuffer);
                    if (reverseBuffer) CFRelease(reverseBuffer);
                    releaseBuffers();
                    return;
                }

                VisionFlowTextures textures;
                textures.metadata = metadata;
                textures.metadata.revision = static_cast<uint32_t>(revision);

                if (!makeFlowTexture(textureCache,
                                     forwardBuffer,
                                     textures.forwardTexture,
                                     textures.forwardKeeper)) {
                    uint64_t count = droppedResults.fetch_add(1, std::memory_order_relaxed) + 1;
                    logDrop("forward Metal alias", metadata, count);
                    if (forwardBuffer) CFRelease(forwardBuffer);
                    if (reverseBuffer) CFRelease(reverseBuffer);
                    releaseBuffers();
                    return;
                }

                if (reverseBuffer &&
                    CVPixelBufferGetWidth(reverseBuffer) == metadata.width &&
                    CVPixelBufferGetHeight(reverseBuffer) == metadata.height &&
                    makeFlowTexture(textureCache,
                                    reverseBuffer,
                                    textures.reverseTexture,
                                    textures.reverseKeeper)) {
                    textures.metadata.hasReverseFlow = true;
                } else {
                    textures.metadata.hasReverseFlow = false;
                }

                {
                    std::lock_guard<std::mutex> lock(latestMutex);
                    latestTextures = std::move(textures);
                }

                completedResults.fetch_add(1, std::memory_order_relaxed);

                if (forwardBuffer) CFRelease(forwardBuffer);
                if (reverseBuffer) CFRelease(reverseBuffer);
                releaseBuffers();
            }
        });
    }

    uint64_t droppedResultCount() const
    {
        return droppedResults.load(std::memory_order_relaxed);
    }

    __strong id<MTLDevice> metalDevice = nil;
    CVMetalTextureCacheRef textureCache = nullptr;
    dispatch_queue_t       workerQueue = nullptr;
    NSUInteger             revision = 0;

    std::atomic<bool>     enabled   { true };
    std::atomic<bool>     available { true };
    std::atomic<uint64_t> latestRequestedCurrFrameID { 0 };
    std::atomic<uint64_t> droppedResults   { 0 };
    std::atomic<uint64_t> completedResults { 0 };

    mutable std::mutex latestMutex;
    VisionFlowTextures latestTextures;
};

VisionOpticalFlow::VisionOpticalFlow(MTL::Device* device)
    : m_impl(std::make_unique<Impl>(device))
{
}

VisionOpticalFlow::~VisionOpticalFlow() = default;

void VisionOpticalFlow::setEnabled(bool enabled)
{
    m_impl->setEnabled(enabled);
}

bool VisionOpticalFlow::isEnabled() const
{
    return m_impl->isEnabled();
}

bool VisionOpticalFlow::isAvailable() const
{
    return m_impl->isAvailable();
}

void VisionOpticalFlow::enqueueFlowPair(CVPixelBufferRef prevPixelBuffer,
                                        CVPixelBufferRef currPixelBuffer,
                                        const VisionFlowPairMetadata& metadata)
{
    m_impl->enqueueFlowPair(prevPixelBuffer, currPixelBuffer, metadata);
}

bool VisionOpticalFlow::copyLatestMatchingFlow(uint64_t prevFrameID,
                                               uint64_t currFrameID,
                                               VisionFlowTextures& outTextures) const
{
    return m_impl->copyLatestMatchingFlow(prevFrameID, currFrameID, outTextures);
}

uint32_t VisionOpticalFlow::revision() const
{
    return static_cast<uint32_t>(m_impl->revision);
}

uint64_t VisionOpticalFlow::droppedResultCount() const
{
    return m_impl->droppedResultCount();
}

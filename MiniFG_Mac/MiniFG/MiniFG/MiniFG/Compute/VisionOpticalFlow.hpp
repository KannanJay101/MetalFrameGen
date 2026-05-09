#pragma once

#include <CoreFoundation/CoreFoundation.h>
#include <CoreVideo/CoreVideo.h>
#include <Metal/Metal.hpp>

#include <cstdint>
#include <memory>

struct VisionFlowPairMetadata
{
    uint64_t prevFrameID    = 0;
    uint64_t currFrameID    = 0;
    uint32_t width          = 0;
    uint32_t height         = 0;
    double   prevTimestamp  = 0.0;
    double   currTimestamp  = 0.0;
    uint32_t revision       = 0;
    bool     hasReverseFlow = false;
};

struct VisionFlowTextures
{
    VisionFlowPairMetadata metadata {};
    MTL::Texture*          forwardTexture = nullptr;
    void*                  forwardKeeper  = nullptr;
    MTL::Texture*          reverseTexture = nullptr;
    void*                  reverseKeeper  = nullptr;

    VisionFlowTextures() = default;
    ~VisionFlowTextures() { reset(); }

    VisionFlowTextures(const VisionFlowTextures& other)
    {
        *this = other;
    }

    VisionFlowTextures& operator=(const VisionFlowTextures& other)
    {
        if (this == &other) return *this;

        reset();
        metadata = other.metadata;

        forwardTexture = other.forwardTexture;
        if (forwardTexture) forwardTexture->retain();
        forwardKeeper = other.forwardKeeper;
        if (forwardKeeper) CFRetain((CFTypeRef)forwardKeeper);

        reverseTexture = other.reverseTexture;
        if (reverseTexture) reverseTexture->retain();
        reverseKeeper = other.reverseKeeper;
        if (reverseKeeper) CFRetain((CFTypeRef)reverseKeeper);

        return *this;
    }

    VisionFlowTextures(VisionFlowTextures&& other) noexcept
    {
        *this = std::move(other);
    }

    VisionFlowTextures& operator=(VisionFlowTextures&& other) noexcept
    {
        if (this == &other) return *this;

        reset();
        metadata       = other.metadata;
        forwardTexture = other.forwardTexture;
        forwardKeeper  = other.forwardKeeper;
        reverseTexture = other.reverseTexture;
        reverseKeeper  = other.reverseKeeper;

        other.metadata = {};
        other.forwardTexture = nullptr;
        other.forwardKeeper  = nullptr;
        other.reverseTexture = nullptr;
        other.reverseKeeper  = nullptr;
        return *this;
    }

    bool valid() const { return forwardTexture != nullptr; }
    bool hasReverse() const { return reverseTexture != nullptr; }

    void reset()
    {
        if (forwardTexture) forwardTexture->release();
        if (forwardKeeper)  CFRelease((CFTypeRef)forwardKeeper);
        if (reverseTexture) reverseTexture->release();
        if (reverseKeeper)  CFRelease((CFTypeRef)reverseKeeper);

        metadata = {};
        forwardTexture = nullptr;
        forwardKeeper  = nullptr;
        reverseTexture = nullptr;
        reverseKeeper  = nullptr;
    }
};

class VisionOpticalFlow
{
public:
    explicit VisionOpticalFlow(MTL::Device* device);
    ~VisionOpticalFlow();

    VisionOpticalFlow(const VisionOpticalFlow&) = delete;
    VisionOpticalFlow& operator=(const VisionOpticalFlow&) = delete;

    void setEnabled(bool enabled);
    bool isEnabled() const;
    bool isAvailable() const;

    void enqueueFlowPair(CVPixelBufferRef prevPixelBuffer,
                         CVPixelBufferRef currPixelBuffer,
                         const VisionFlowPairMetadata& metadata);

    bool copyLatestMatchingFlow(uint64_t prevFrameID,
                                uint64_t currFrameID,
                                VisionFlowTextures& outTextures) const;

    uint32_t revision() const;
    uint64_t droppedResultCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

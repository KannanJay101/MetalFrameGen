//-------------------------------------------------------------------------------------------------------------------------------------------------------------
//
// MetalFX/MTLFXFrameInterpolator.hpp
//
// Copyright 2020-2024 Apple Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//-------------------------------------------------------------------------------------------------------------------------------------------------------------

#pragma once

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

#include "MTLFXDefines.hpp"
#include "MTLFXPrivate.hpp"

#include "../Metal/Metal.hpp"

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

namespace MTLFX
{
    class FrameInterpolatorDescriptor : public NS::Copying< FrameInterpolatorDescriptor >
    {
    public:
        static class FrameInterpolatorDescriptor*   alloc();
        class FrameInterpolatorDescriptor*          init();

        MTL::PixelFormat                            colorTextureFormat() const;
        void                                        setColorTextureFormat( MTL::PixelFormat format );

        MTL::PixelFormat                            depthTextureFormat() const;
        void                                        setDepthTextureFormat( MTL::PixelFormat format );

        MTL::PixelFormat                            motionTextureFormat() const;
        void                                        setMotionTextureFormat( MTL::PixelFormat format );

        MTL::PixelFormat                            outputTextureFormat() const;
        void                                        setOutputTextureFormat( MTL::PixelFormat format );

        NS::UInteger                                width() const;
        void                                        setWidth( NS::UInteger width );

        NS::UInteger                                height() const;
        void                                        setHeight( NS::UInteger height );

        bool                                        isDepthReversed() const;
        void                                        setDepthReversed( bool depthReversed );

        bool                                        requiresSynchronousInitialization() const;
        void                                        setRequiresSynchronousInitialization( bool requiresSynchronousInitialization );

        class FrameInterpolator*                    newFrameInterpolator( const MTL::Device* pDevice ) const;

        static bool                                 supportsDevice( const MTL::Device* pDevice );
    };

    class FrameInterpolator : public NS::Referencing< FrameInterpolator >
    {
    public:
        // Per-frame input/output textures
        MTL::Texture*                               colorTexture() const;
        void                                        setColorTexture( MTL::Texture* pTexture );

        MTL::Texture*                               previousColorTexture() const;
        void                                        setPreviousColorTexture( MTL::Texture* pTexture );

        MTL::Texture*                               depthTexture() const;
        void                                        setDepthTexture( MTL::Texture* pTexture );

        MTL::Texture*                               motionTexture() const;
        void                                        setMotionTexture( MTL::Texture* pTexture );

        MTL::Texture*                               outputTexture() const;
        void                                        setOutputTexture( MTL::Texture* pTexture );

        // Motion vector scale
        float                                       motionVectorScaleX() const;
        void                                        setMotionVectorScaleX( float scale );

        float                                       motionVectorScaleY() const;
        void                                        setMotionVectorScaleY( float scale );

        // Per-frame state
        bool                                        reset() const;
        void                                        setReset( bool reset );

        bool                                        isDepthReversed() const;
        void                                        setDepthReversed( bool depthReversed );

        MTL::Fence*                                 fence() const;
        void                                        setFence( MTL::Fence* pFence );

        // Required texture usage hints (read-only)
        MTL::TextureUsage                           colorTextureUsage() const;
        MTL::TextureUsage                           previousColorTextureUsage() const;
        MTL::TextureUsage                           depthTextureUsage() const;
        MTL::TextureUsage                           motionTextureUsage() const;
        MTL::TextureUsage                           outputTextureUsage() const;

        // Immutable descriptor properties (read-only)
        MTL::PixelFormat                            colorTextureFormat() const;
        MTL::PixelFormat                            depthTextureFormat() const;
        MTL::PixelFormat                            motionTextureFormat() const;
        MTL::PixelFormat                            outputTextureFormat() const;
        NS::UInteger                                width() const;
        NS::UInteger                                height() const;

        void                                        encodeToCommandBuffer( MTL::CommandBuffer* pCommandBuffer );
    };
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------
// FrameInterpolatorDescriptor inline implementations
//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTLFX::FrameInterpolatorDescriptor* MTLFX::FrameInterpolatorDescriptor::alloc()
{
    return NS::Object::alloc< FrameInterpolatorDescriptor >( _MTLFX_PRIVATE_CLS( MTLFXFrameInterpolatorDescriptor ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTLFX::FrameInterpolatorDescriptor* MTLFX::FrameInterpolatorDescriptor::init()
{
    return NS::Object::init< FrameInterpolatorDescriptor >();
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::PixelFormat MTLFX::FrameInterpolatorDescriptor::colorTextureFormat() const
{
    return Object::sendMessage< MTL::PixelFormat >( this, _MTLFX_PRIVATE_SEL( colorTextureFormat ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolatorDescriptor::setColorTextureFormat( MTL::PixelFormat format )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setColorTextureFormat_ ), format );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::PixelFormat MTLFX::FrameInterpolatorDescriptor::depthTextureFormat() const
{
    return Object::sendMessage< MTL::PixelFormat >( this, _MTLFX_PRIVATE_SEL( depthTextureFormat ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolatorDescriptor::setDepthTextureFormat( MTL::PixelFormat format )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setDepthTextureFormat_ ), format );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::PixelFormat MTLFX::FrameInterpolatorDescriptor::motionTextureFormat() const
{
    return Object::sendMessage< MTL::PixelFormat >( this, _MTLFX_PRIVATE_SEL( motionTextureFormat ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolatorDescriptor::setMotionTextureFormat( MTL::PixelFormat format )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setMotionTextureFormat_ ), format );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::PixelFormat MTLFX::FrameInterpolatorDescriptor::outputTextureFormat() const
{
    return Object::sendMessage< MTL::PixelFormat >( this, _MTLFX_PRIVATE_SEL( outputTextureFormat ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolatorDescriptor::setOutputTextureFormat( MTL::PixelFormat format )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setOutputTextureFormat_ ), format );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE NS::UInteger MTLFX::FrameInterpolatorDescriptor::width() const
{
    return Object::sendMessage< NS::UInteger >( this, _MTLFX_PRIVATE_SEL( width ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolatorDescriptor::setWidth( NS::UInteger width )
{
    Object::sendMessage< void >( this, _MTLFX_PRIVATE_SEL( setWidth_ ), width );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE NS::UInteger MTLFX::FrameInterpolatorDescriptor::height() const
{
    return Object::sendMessage< NS::UInteger >( this, _MTLFX_PRIVATE_SEL( height ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolatorDescriptor::setHeight( NS::UInteger height )
{
    Object::sendMessage< void >( this, _MTLFX_PRIVATE_SEL( setHeight_ ), height );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE bool MTLFX::FrameInterpolatorDescriptor::isDepthReversed() const
{
    return Object::sendMessage< bool >( this, _MTLFX_PRIVATE_SEL( isDepthReversed ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolatorDescriptor::setDepthReversed( bool depthReversed )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setDepthReversed_ ), depthReversed );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE bool MTLFX::FrameInterpolatorDescriptor::requiresSynchronousInitialization() const
{
    return Object::sendMessage< bool >( this, _MTL_PRIVATE_SEL( requiresSynchronousInitialization ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolatorDescriptor::setRequiresSynchronousInitialization( bool requiresSynchronousInitialization )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setRequiresSynchronousInitialization_ ), requiresSynchronousInitialization );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTLFX::FrameInterpolator* MTLFX::FrameInterpolatorDescriptor::newFrameInterpolator( const MTL::Device* pDevice ) const
{
    return Object::sendMessage< FrameInterpolator* >( this, _MTLFX_PRIVATE_SEL( newFrameInterpolatorWithDevice_ ), pDevice );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE bool MTLFX::FrameInterpolatorDescriptor::supportsDevice( const MTL::Device* pDevice )
{
    return Object::sendMessageSafe< bool >( _NS_PRIVATE_CLS( MTLFXFrameInterpolatorDescriptor ), _MTLFX_PRIVATE_SEL( supportsDevice_ ), pDevice );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------
// FrameInterpolator inline implementations
//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::Texture* MTLFX::FrameInterpolator::colorTexture() const
{
    return Object::sendMessage< MTL::Texture* >( this, _MTLFX_PRIVATE_SEL( colorTexture ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolator::setColorTexture( MTL::Texture* pTexture )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setColorTexture_ ), pTexture );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::Texture* MTLFX::FrameInterpolator::previousColorTexture() const
{
    return Object::sendMessage< MTL::Texture* >( this, _MTLFX_PRIVATE_SEL( previousColorTexture ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolator::setPreviousColorTexture( MTL::Texture* pTexture )
{
    Object::sendMessage< void >( this, _MTLFX_PRIVATE_SEL( setPreviousColorTexture_ ), pTexture );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::Texture* MTLFX::FrameInterpolator::depthTexture() const
{
    return Object::sendMessage< MTL::Texture* >( this, _MTLFX_PRIVATE_SEL( depthTexture ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolator::setDepthTexture( MTL::Texture* pTexture )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setDepthTexture_ ), pTexture );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::Texture* MTLFX::FrameInterpolator::motionTexture() const
{
    return Object::sendMessage< MTL::Texture* >( this, _MTLFX_PRIVATE_SEL( motionTexture ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolator::setMotionTexture( MTL::Texture* pTexture )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setMotionTexture_ ), pTexture );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::Texture* MTLFX::FrameInterpolator::outputTexture() const
{
    return Object::sendMessage< MTL::Texture* >( this, _MTLFX_PRIVATE_SEL( outputTexture ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolator::setOutputTexture( MTL::Texture* pTexture )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setOutputTexture_ ), pTexture );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE float MTLFX::FrameInterpolator::motionVectorScaleX() const
{
    return Object::sendMessage< float >( this, _MTLFX_PRIVATE_SEL( motionVectorScaleX ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolator::setMotionVectorScaleX( float scale )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setMotionVectorScaleX_ ), scale );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE float MTLFX::FrameInterpolator::motionVectorScaleY() const
{
    return Object::sendMessage< float >( this, _MTLFX_PRIVATE_SEL( motionVectorScaleY ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolator::setMotionVectorScaleY( float scale )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setMotionVectorScaleY_ ), scale );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE bool MTLFX::FrameInterpolator::reset() const
{
    return Object::sendMessage< bool >( this, _MTLFX_PRIVATE_SEL( reset ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolator::setReset( bool reset )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setReset_ ), reset );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE bool MTLFX::FrameInterpolator::isDepthReversed() const
{
    return Object::sendMessage< bool >( this, _MTLFX_PRIVATE_SEL( isDepthReversed ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolator::setDepthReversed( bool depthReversed )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setDepthReversed_ ), depthReversed );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::Fence* MTLFX::FrameInterpolator::fence() const
{
    return Object::sendMessage< MTL::Fence* >( this, _MTLFX_PRIVATE_SEL( fence ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolator::setFence( MTL::Fence* pFence )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( setFence_ ), pFence );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::TextureUsage MTLFX::FrameInterpolator::colorTextureUsage() const
{
    return Object::sendMessage< MTL::TextureUsage >( this, _MTLFX_PRIVATE_SEL( colorTextureUsage ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::TextureUsage MTLFX::FrameInterpolator::previousColorTextureUsage() const
{
    return Object::sendMessage< MTL::TextureUsage >( this, _MTLFX_PRIVATE_SEL( previousColorTextureUsage ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::TextureUsage MTLFX::FrameInterpolator::depthTextureUsage() const
{
    return Object::sendMessage< MTL::TextureUsage >( this, _MTLFX_PRIVATE_SEL( depthTextureUsage ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::TextureUsage MTLFX::FrameInterpolator::motionTextureUsage() const
{
    return Object::sendMessage< MTL::TextureUsage >( this, _MTLFX_PRIVATE_SEL( motionTextureUsage ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::TextureUsage MTLFX::FrameInterpolator::outputTextureUsage() const
{
    return Object::sendMessage< MTL::TextureUsage >( this, _MTLFX_PRIVATE_SEL( outputTextureUsage ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::PixelFormat MTLFX::FrameInterpolator::colorTextureFormat() const
{
    return Object::sendMessage< MTL::PixelFormat >( this, _MTLFX_PRIVATE_SEL( colorTextureFormat ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::PixelFormat MTLFX::FrameInterpolator::depthTextureFormat() const
{
    return Object::sendMessage< MTL::PixelFormat >( this, _MTLFX_PRIVATE_SEL( depthTextureFormat ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::PixelFormat MTLFX::FrameInterpolator::motionTextureFormat() const
{
    return Object::sendMessage< MTL::PixelFormat >( this, _MTLFX_PRIVATE_SEL( motionTextureFormat ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE MTL::PixelFormat MTLFX::FrameInterpolator::outputTextureFormat() const
{
    return Object::sendMessage< MTL::PixelFormat >( this, _MTLFX_PRIVATE_SEL( outputTextureFormat ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE NS::UInteger MTLFX::FrameInterpolator::width() const
{
    return Object::sendMessage< NS::UInteger >( this, _MTLFX_PRIVATE_SEL( width ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE NS::UInteger MTLFX::FrameInterpolator::height() const
{
    return Object::sendMessage< NS::UInteger >( this, _MTLFX_PRIVATE_SEL( height ) );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

_MTLFX_INLINE void MTLFX::FrameInterpolator::encodeToCommandBuffer( MTL::CommandBuffer* pCommandBuffer )
{
    Object::sendMessage< void >( this, _MTL_PRIVATE_SEL( encodeToCommandBuffer_ ), pCommandBuffer );
}

//-------------------------------------------------------------------------------------------------------------------------------------------------------------

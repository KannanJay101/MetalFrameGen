import Foundation
import Metal
import QuartzCore
import CoreVideo

enum OpticalFlowDebugMode: Int, CaseIterable, Identifiable {
    case output = 0
    case flow = 1
    case confidence = 2
    case split = 3

    var id: Int { rawValue }

    var title: String {
        switch self {
        case .output: return "Output"
        case .flow: return "Flow"
        case .confidence: return "Confidence"
        case .split: return "Split"
        }
    }
}

/// Swift wrapper that owns a MetalEngine C++ object via the ObjC shim.
final class EngineBridge {

    private let native: MetalEngineObjC
    private let device: MTLDevice
    private var textureCache: CVMetalTextureCache?
    private var submittedFrames = 0
    private var fallbackUploadFrames = 0
    private var droppedFrames = 0

    init(device: MTLDevice) {
        self.device = device
        native = MetalEngineObjC(device: device)

        var cache: CVMetalTextureCache?
        let status = CVMetalTextureCacheCreate(
            kCFAllocatorDefault, nil, device, nil, &cache)
        if status != kCVReturnSuccess {
            print("[EngineBridge] CVMetalTextureCacheCreate failed: \(status)")
        }
        self.textureCache = cache
    }

    func configure(layer: CAMetalLayer) {
        native.configure(with: layer)
    }

    func resize(width: UInt32, height: UInt32) {
        native.resizeWidth(width, height: height)
    }

    // Pixel format is coupled to StreamManager's config.pixelFormat
    // (kCVPixelFormatType_32BGRA). If that changes, update .bgra8Unorm below.
    func submitFrame(buffer: CVPixelBuffer,
                     width: UInt32,
                     height: UInt32,
                     captureTimestamp: Double) {
        guard let cache = textureCache else {
            submitFrameByCPUUpload(
                buffer: buffer,
                width: width,
                height: height,
                captureTimestamp: captureTimestamp,
                reason: "missing CVMetalTextureCache"
            )
            return
        }

        var cvTex: CVMetalTexture?
        let status = CVMetalTextureCacheCreateTextureFromImage(
            kCFAllocatorDefault, cache, buffer, nil,
            .bgra8Unorm, Int(width), Int(height), 0, &cvTex)

        guard status == kCVReturnSuccess,
              let cvTex,
              let mtlTex = CVMetalTextureGetTexture(cvTex)
        else {
            submitFrameByCPUUpload(
                buffer: buffer,
                width: width,
                height: height,
                captureTimestamp: captureTimestamp,
                reason: "CVMetalTextureCacheCreateTextureFromImage status \(status)"
            )
            return
        }

        native.submitTexture(
            mtlTex,
            keeper: cvTex,
            pixelBuffer: buffer,
            captureTimestamp: captureTimestamp
        )

        // CVMetalTextureCache is cheap to reuse. Flushing every frame adds
        // driver churn; the engine keeps only its small ring of CVMetalTexture
        // keepers alive, so occasional cleanup is enough.
        submittedFrames &+= 1
        if submittedFrames % 240 == 0 {
            CVMetalTextureCacheFlush(cache, 0)
        }
    }

    private func submitFrameByCPUUpload(buffer: CVPixelBuffer,
                                        width: UInt32,
                                        height: UInt32,
                                        captureTimestamp: Double,
                                        reason: String) {
        fallbackUploadFrames &+= 1
        if fallbackUploadFrames <= 5 || fallbackUploadFrames % 120 == 0 {
            print("[EngineBridge] Zero-copy submit unavailable (\(reason)); using CPU upload fallback [\(fallbackUploadFrames)]")
        }

        let lockStatus = CVPixelBufferLockBaseAddress(buffer, .readOnly)
        guard lockStatus == kCVReturnSuccess else {
            logDroppedFrame("CVPixelBufferLockBaseAddress status \(lockStatus)")
            return
        }
        defer { CVPixelBufferUnlockBaseAddress(buffer, .readOnly) }

        guard let baseAddress = CVPixelBufferGetBaseAddress(buffer) else {
            logDroppedFrame("missing pixel buffer base address")
            return
        }

        let descriptor = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .bgra8Unorm,
            width: Int(width),
            height: Int(height),
            mipmapped: false
        )
        descriptor.usage = [.shaderRead]
        descriptor.storageMode = .shared

        guard let texture = device.makeTexture(descriptor: descriptor) else {
            logDroppedFrame("MTLDevice.makeTexture failed")
            return
        }

        let region = MTLRegionMake2D(0, 0, Int(width), Int(height))
        texture.replace(region: region,
                        mipmapLevel: 0,
                        withBytes: baseAddress,
                        bytesPerRow: CVPixelBufferGetBytesPerRow(buffer))

        native.submitTexture(
            texture,
            keeper: nil,
            pixelBuffer: buffer,
            captureTimestamp: captureTimestamp
        )
    }

    private func logDroppedFrame(_ reason: String) {
        droppedFrames &+= 1
        if droppedFrames <= 5 || droppedFrames % 120 == 0 {
            print("[EngineBridge] Dropped frame: \(reason) [\(droppedFrames)]")
        }
    }

    func renderFrame() {
        native.renderFrame()
    }

    func setDisplayRefreshPeriod(_ seconds: Double) {
        native.setDisplayRefreshPeriod(seconds)
    }

    func setOpticalFlowEnabled(_ enabled: Bool) {
        native.setOpticalFlowEnabled(enabled)
    }

    func setOpticalFlowDebugMode(_ mode: OpticalFlowDebugMode) {
        native.setOpticalFlowDebugMode(UInt32(mode.rawValue))
    }

    var renderCount: UInt64 {
        native.renderCount()
    }

    var interpCount: UInt64 {
        native.interpCount()
    }

    var flowInterpCount: UInt64 {
        native.flowInterpCount()
    }
}

import Foundation
import Metal
import QuartzCore
import CoreVideo

/// Swift wrapper that owns a MetalEngine C++ object via the ObjC shim.
final class EngineBridge {

    private let native: MetalEngineObjC

    init(device: MTLDevice) {
        native = MetalEngineObjC(device: device)
    }

    func configure(layer: CAMetalLayer) {
        native.configure(with: layer)
    }

    func resize(width: UInt32, height: UInt32) {
        native.resizeWidth(width, height: height)
    }

    /// Submit a captured frame. The CVPixelBuffer is forwarded to the engine
    /// so the Vision optical-flow worker can compute (prev, curr) flow
    /// asynchronously; the engine still does its own CPU upload to the input
    /// texture ring so the crossfade fallback path keeps working when flow
    /// isn't ready or is disabled.
    func submitFrame(buffer: CVPixelBuffer,
                     width: UInt32,
                     height: UInt32,
                     captureTimestamp: Double = 0) {
        native.submitFrame(
            with: buffer,
            width: width,
            height: height,
            captureTimestamp: captureTimestamp
        )
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

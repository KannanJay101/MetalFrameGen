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

    func submitFrame(buffer: CVPixelBuffer, width: UInt32, height: UInt32) {
        CVPixelBufferLockBaseAddress(buffer, .readOnly)
        defer { CVPixelBufferUnlockBaseAddress(buffer, .readOnly) }

        guard let baseAddr = CVPixelBufferGetBaseAddress(buffer) else { return }
        let bytesPerRow = CVPixelBufferGetBytesPerRow(buffer)

        native.submitFrame(
            withPixels: baseAddr,
            width: width,
            height: height,
            bytesPerRow: bytesPerRow
        )
    }

    func renderFrame() {
        native.renderFrame()
    }

    var renderCount: UInt64 {
        native.renderCount()
    }

    var interpCount: UInt64 {
        native.interpCount()
    }
}

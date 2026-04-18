import ScreenCaptureKit
import CoreMedia
import CoreVideo

typealias FrameBufferCallback = (CVPixelBuffer, UInt32, UInt32) -> Void

/// Tracks capture statistics for display in the UI.
final class CaptureStats: ObservableObject, Sendable {
    // Atomically updated from the capture queue
    private let _capturedFrames = OSAtomicCounter()
    private let _renderedFrames = OSAtomicCounter()
    private let _lastCaptureWidth = OSAtomicInt()
    private let _lastCaptureHeight = OSAtomicInt()
    private let _captureStartTime = OSAtomicDouble()
    private let _lastFPSSample = OSAtomicDouble()
    private let _lastFPSFrameCount = OSAtomicCounter()

    var capturedFrames: Int { _capturedFrames.value }
    var renderedFrames: Int { _renderedFrames.value }
    var lastCaptureWidth: Int { _lastCaptureWidth.value }
    var lastCaptureHeight: Int { _lastCaptureHeight.value }

    var captureFPS: Double {
        let now = CFAbsoluteTimeGetCurrent()
        let elapsed = now - _lastFPSSample.value
        guard elapsed > 0.001 else { return 0 }
        let frames = _capturedFrames.value - _lastFPSFrameCount.value
        return Double(frames) / elapsed
    }

    func recordCapture(width: Int, height: Int) {
        _capturedFrames.increment()
        _lastCaptureWidth.set(width)
        _lastCaptureHeight.set(height)

        if _captureStartTime.value == 0 {
            let now = CFAbsoluteTimeGetCurrent()
            _captureStartTime.set(now)
            _lastFPSSample.set(now)
        }
    }

    func recordRender() {
        _renderedFrames.increment()
    }

    func sampleFPS() -> Double {
        let now = CFAbsoluteTimeGetCurrent()
        let elapsed = now - _lastFPSSample.value
        guard elapsed > 0.001 else { return 0 }
        let frames = _capturedFrames.value - _lastFPSFrameCount.value
        let fps = Double(frames) / elapsed
        _lastFPSSample.set(now)
        _lastFPSFrameCount.set(_capturedFrames.value)
        return fps
    }

    func reset() {
        _capturedFrames.set(0)
        _renderedFrames.set(0)
        _lastCaptureWidth.set(0)
        _lastCaptureHeight.set(0)
        _captureStartTime.set(0)
        _lastFPSSample.set(0)
        _lastFPSFrameCount.set(0)
    }
}

// Lock-free atomic helpers for cross-thread stats
private final class OSAtomicCounter: Sendable {
    private let storage = UnsafeMutablePointer<Int>.allocate(capacity: 1)
    init() { storage.initialize(to: 0) }
    deinit { storage.deallocate() }
    var value: Int { storage.pointee }
    func increment() { OSAtomicIncrement64(UnsafeMutablePointer<Int64>(OpaquePointer(storage))) }
    func set(_ v: Int) { storage.pointee = v }
}

private final class OSAtomicInt: Sendable {
    private let storage = UnsafeMutablePointer<Int>.allocate(capacity: 1)
    init() { storage.initialize(to: 0) }
    deinit { storage.deallocate() }
    var value: Int { storage.pointee }
    func set(_ v: Int) { storage.pointee = v }
}

private final class OSAtomicDouble: Sendable {
    private let storage = UnsafeMutablePointer<Double>.allocate(capacity: 1)
    init() { storage.initialize(to: 0) }
    deinit { storage.deallocate() }
    var value: Double { storage.pointee }
    func set(_ v: Double) { storage.pointee = v }
}

/// Manages an SCStream for a single SCWindow and vends captured frames
/// via a callback on a high-priority background queue.
final class StreamManager: NSObject {

    private var stream: SCStream?
    private var frameCallback: FrameBufferCallback?
    let stats = CaptureStats()
    private let outputQueue = DispatchQueue(
        label: "com.minifg.capture",
        qos: .userInteractive
    )

    func start(window: SCWindow, callback: @escaping FrameBufferCallback) async throws {
        self.frameCallback = callback

        let filter = SCContentFilter(desktopIndependentWindow: window)

        let scale = NSScreen.main?.backingScaleFactor ?? 2.0
        let config = SCStreamConfiguration()
        config.width               = Int(window.frame.width * scale)
        config.height              = Int(window.frame.height * scale)
        config.minimumFrameInterval = CMTime(value: 1, timescale: 60)
        config.pixelFormat          = kCVPixelFormatType_32BGRA
        config.showsCursor          = false
        config.queueDepth           = 3

        print("[StreamManager] Starting capture: \(config.width)x\(config.height) @ up to 60Hz")

        let s = SCStream(filter: filter, configuration: config, delegate: self)
        try s.addStreamOutput(self, type: .screen, sampleHandlerQueue: outputQueue)
        try await s.startCapture()
        self.stream = s
        print("[StreamManager] Capture started successfully")
    }

    func stop() {
        print("[StreamManager] Stopping capture. Total captured: \(stats.capturedFrames), rendered: \(stats.renderedFrames)")
        Task { try? await stream?.stopCapture() }
        stream = nil
    }
}

extension StreamManager: SCStreamOutput {
    func stream(
        _ stream: SCStream,
        didOutputSampleBuffer sampleBuffer: CMSampleBuffer,
        of type: SCStreamOutputType
    ) {
        guard type == .screen,
              let pixelBuffer = sampleBuffer.imageBuffer else { return }

        let width  = UInt32(CVPixelBufferGetWidth(pixelBuffer))
        let height = UInt32(CVPixelBufferGetHeight(pixelBuffer))
        stats.recordCapture(width: Int(width), height: Int(height))

        // Log first frame and every 300 frames after
        let count = stats.capturedFrames
        if count == 1 {
            print("[StreamManager] First frame captured: \(width)x\(height)")
        } else if count % 300 == 0 {
            let fps = stats.sampleFPS()
            print("[StreamManager] Captured \(count) frames | \(width)x\(height) | ~\(String(format: "%.1f", fps)) capture FPS")
        }

        frameCallback?(pixelBuffer, width, height)
    }
}

extension StreamManager: SCStreamDelegate {
    func stream(_ stream: SCStream, didStopWithError error: Error) {
        print("[StreamManager] Stream stopped with error: \(error)")
    }
}

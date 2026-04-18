import AppKit
import Foundation
import ScreenCaptureKit
import CoreMedia
import CoreVideo

typealias FrameBufferCallback = (CVPixelBuffer, UInt32, UInt32) -> Void

/// Tracks capture statistics for display in the UI.
final class CaptureStats: ObservableObject, Sendable {
    private struct State {
        var capturedFrames = 0
        var renderedFrames = 0
        var lastCaptureWidth = 0
        var lastCaptureHeight = 0
        var captureStartTime = 0.0
        var lastFPSSample = 0.0
        var lastFPSFrameCount = 0
    }

    private let lock = NSLock()
    private var state = State()

    var capturedFrames: Int { withState { $0.capturedFrames } }
    var renderedFrames: Int { withState { $0.renderedFrames } }
    var lastCaptureWidth: Int { withState { $0.lastCaptureWidth } }
    var lastCaptureHeight: Int { withState { $0.lastCaptureHeight } }

    var captureFPS: Double {
        let now = CFAbsoluteTimeGetCurrent()
        let snapshot = withState { $0 }
        let elapsed = now - snapshot.lastFPSSample
        guard elapsed > 0.001 else { return 0 }
        let frames = snapshot.capturedFrames - snapshot.lastFPSFrameCount
        return Double(frames) / elapsed
    }

    func recordCapture(width: Int, height: Int) {
        withState {
            let now = CFAbsoluteTimeGetCurrent()
            $0.capturedFrames += 1
            $0.lastCaptureWidth = width
            $0.lastCaptureHeight = height

            if $0.captureStartTime == 0 {
                $0.captureStartTime = now
                $0.lastFPSSample = now
            }
        }
    }

    func recordRender() {
        withState { $0.renderedFrames += 1 }
    }

    func sampleFPS() -> Double {
        let now = CFAbsoluteTimeGetCurrent()
        return withState {
            let elapsed = now - $0.lastFPSSample
            guard elapsed > 0.001 else { return 0 }
            let frames = $0.capturedFrames - $0.lastFPSFrameCount
            let fps = Double(frames) / elapsed
            $0.lastFPSSample = now
            $0.lastFPSFrameCount = $0.capturedFrames
            return fps
        }
    }

    func reset() {
        withState { $0 = State() }
    }

    private func withState<T>(_ body: (inout State) -> T) -> T {
        lock.lock()
        defer { lock.unlock() }
        return body(&state)
    }
}

/// Manages an SCStream for a single SCWindow and vends captured frames
/// via a callback on a high-priority background queue.
final class StreamManager: NSObject {

    private var stream: SCStream?
    private var frameCallback: FrameBufferCallback?
    let stats = CaptureStats()
    private let outputQueue = DispatchQueue(
        label: "com.minifg.capture",
        qos: .userInitiated
    )

    func start(window: SCWindow, callback: @escaping FrameBufferCallback) async throws {
        stats.reset()
        self.frameCallback = callback

        let filter = SCContentFilter(desktopIndependentWindow: window)

        let scale = captureScale(for: window)
        let config = SCStreamConfiguration()
        config.width                = Int(window.frame.width * scale)
        config.height               = Int(window.frame.height * scale)
        config.minimumFrameInterval = CMTime(value: 1, timescale: 60)
        config.pixelFormat          = kCVPixelFormatType_32BGRA
        config.showsCursor          = false
        config.queueDepth           = 2

        print("[StreamManager] Starting capture: \(config.width)x\(config.height) at 60 FPS on scale \(String(format: "%.2f", scale))")

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
        frameCallback = nil
    }

    private func captureScale(for window: SCWindow) -> CGFloat {
        guard let screen = NSScreen.screens.max(by: {
            intersectionArea($0.frame, window.frame) < intersectionArea($1.frame, window.frame)
        }) else {
            return NSScreen.main?.backingScaleFactor ?? 2.0
        }
        return screen.backingScaleFactor
    }

    private func intersectionArea(_ lhs: CGRect, _ rhs: CGRect) -> CGFloat {
        let rect = lhs.intersection(rhs)
        guard !rect.isNull, !rect.isEmpty else { return 0 }
        return rect.width * rect.height
    }
}

extension StreamManager: SCStreamOutput {
    func stream(
        _ stream: SCStream,
        didOutputSampleBuffer sampleBuffer: CMSampleBuffer,
        of type: SCStreamOutputType
    ) {
        guard frameStatus(for: sampleBuffer) == .complete else { return }
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

    private func frameStatus(for sampleBuffer: CMSampleBuffer) -> SCFrameStatus? {
        guard let attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, createIfNecessary: false)
                as? [[SCStreamFrameInfo: Any]],
              let statusRawValue = attachments.first?[.status] as? Int else {
            return nil
        }
        return SCFrameStatus(rawValue: statusRawValue)
    }
}

extension StreamManager: SCStreamDelegate {
    func stream(_ stream: SCStream, didStopWithError error: Error) {
        print("[StreamManager] Stream stopped with error: \(error)")
    }
}

import AppKit
import QuartzCore
import Metal

/// Owns the transparent, borderless, click-through NSWindow that sits above
/// the target game. Hosts a CAMetalLayer driven by a CVDisplayLink.
final class OverlayWindowController: NSObject {

    let engine: EngineBridge
    private var window: NSWindow!
    private var metalLayer: CAMetalLayer!
    private var displayLink: CVDisplayLink?
    private let device: MTLDevice

    init(frame: CGRect) {
        guard let dev = MTLCreateSystemDefaultDevice() else {
            fatalError("No Metal device available")
        }
        self.device = dev
        self.engine = EngineBridge(device: dev)
        super.init()
        buildWindow(frame)
    }

    private func buildWindow(_ frame: CGRect) {
        let styleMask: NSWindow.StyleMask = [.borderless]
        window = NSWindow(
            contentRect: frame,
            styleMask:   styleMask,
            backing:     .buffered,
            defer:       false
        )
        window.level               = .screenSaver
        window.isOpaque            = false
        window.backgroundColor     = .clear
        window.hasShadow           = false
        window.ignoresMouseEvents  = true
        window.collectionBehavior  = [.canJoinAllSpaces, .fullScreenAuxiliary]

        metalLayer = CAMetalLayer()
        metalLayer.device          = device
        metalLayer.pixelFormat     = .bgra8Unorm
        metalLayer.framebufferOnly = false
        metalLayer.isOpaque        = false
        metalLayer.frame           = CGRect(origin: .zero, size: frame.size)
        metalLayer.contentsScale   = NSScreen.main?.backingScaleFactor ?? 2.0
        window.contentView?.wantsLayer = true
        window.contentView?.layer      = metalLayer

        engine.configure(layer: metalLayer)
    }

    func show() {
        window.orderFrontRegardless()
        startDisplayLink()
    }

    func close() {
        stopDisplayLink()
        window.close()
    }

    private func startDisplayLink() {
        CVDisplayLinkCreateWithActiveCGDisplays(&displayLink)
        guard let dl = displayLink else { return }

        let opaquePtr = UnsafeMutableRawPointer(Unmanaged.passUnretained(self).toOpaque())
        CVDisplayLinkSetOutputCallback(dl, { _, _, _, _, _, ctx -> CVReturn in
            let controller = Unmanaged<OverlayWindowController>
                .fromOpaque(ctx!)
                .takeUnretainedValue()
            controller.engine.renderFrame()
            return kCVReturnSuccess
        }, opaquePtr)

        CVDisplayLinkStart(dl)
    }

    private func stopDisplayLink() {
        if let dl = displayLink { CVDisplayLinkStop(dl) }
        displayLink = nil
    }

    func sync(to frame: CGRect) {
        window.setFrame(frame, display: true)
        metalLayer.frame = CGRect(origin: .zero, size: frame.size)
        let scale = metalLayer.contentsScale
        engine.resize(width: UInt32(frame.width * scale), height: UInt32(frame.height * scale))
    }
}

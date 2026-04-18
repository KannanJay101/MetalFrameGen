import AppKit
import QuartzCore
import Metal
import CoreVideo

/// Owns the transparent, borderless, click-through NSWindow that sits above
/// the target game. Hosts a CAMetalLayer driven by a CVDisplayLink.
final class OverlayWindowController: NSObject {

    let engine: EngineBridge
    private var window: NSWindow!
    private var metalLayer: CAMetalLayer!
    private var displayLink: CVDisplayLink?
    private let device: MTLDevice

    /// Called (on main thread) when the user presses the exit hotkey.
    var onExitHotkey: (() -> Void)?

    private var globalMonitor: Any?
    private var localMonitor: Any?

    init(frame: CGRect) {
        guard let dev = MTLCreateSystemDefaultDevice() else {
            fatalError("No Metal device available")
        }
        self.device = dev
        self.engine = EngineBridge(device: dev)
        super.init()
        buildWindow(frame)
    }

    deinit {
        removeHotkeyMonitors()
    }

    private func buildWindow(_ frame: CGRect) {
        let styleMask: NSWindow.StyleMask = [.borderless]
        window = NSWindow(
            contentRect: frame,
            styleMask:   styleMask,
            backing:     .buffered,
            defer:       false
        )
        window.level               = .floating
        window.isOpaque            = false
        window.backgroundColor     = .clear
        window.hasShadow           = false
        window.ignoresMouseEvents  = true
        window.hidesOnDeactivate   = false
        window.sharingType         = .none
        window.collectionBehavior  = [.canJoinAllSpaces, .fullScreenAuxiliary]

        metalLayer = CAMetalLayer()
        metalLayer.device             = device
        metalLayer.pixelFormat        = .bgra8Unorm
        metalLayer.framebufferOnly    = false
        metalLayer.isOpaque           = false
        metalLayer.displaySyncEnabled = false   // uncapped — semaphore throttles in-flight frames
        metalLayer.frame              = CGRect(origin: .zero, size: frame.size)
        metalLayer.contentsScale      = NSScreen.main?.backingScaleFactor ?? 2.0
        window.contentView?.wantsLayer = true
        window.contentView?.layer      = metalLayer

        engine.configure(layer: metalLayer)
    }

    func show() {
        window.orderFrontRegardless()
        startDisplayLink()
        installHotkeyMonitors()
    }

    func close() {
        removeHotkeyMonitors()
        stopDisplayLink()
        window.orderOut(nil)
    }

    // MARK: - Global hotkey (Escape to exit)

    private func installHotkeyMonitors() {
        // Global monitor: fires when another app (e.g. a game) is focused
        globalMonitor = NSEvent.addGlobalMonitorForEvents(matching: .keyDown) { [weak self] event in
            self?.handleKeyEvent(event)
        }
        // Local monitor: fires when MiniFG itself is focused
        localMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
            if self?.handleKeyEvent(event) == true {
                return nil // consume the event
            }
            return event
        }
    }

    private func removeHotkeyMonitors() {
        if let m = globalMonitor { NSEvent.removeMonitor(m); globalMonitor = nil }
        if let m = localMonitor  { NSEvent.removeMonitor(m); localMonitor = nil }
    }

    @discardableResult
    private func handleKeyEvent(_ event: NSEvent) -> Bool {
        // Escape key (keyCode 53) exits the overlay
        if event.keyCode == 53 {
            DispatchQueue.main.async { [weak self] in
                self?.onExitHotkey?()
            }
            return true
        }
        return false
    }

    // MARK: - CVDisplayLink (renders at display refresh rate)

    private func startDisplayLink() {
        guard CVDisplayLinkCreateWithActiveCGDisplays(&displayLink) == kCVReturnSuccess,
              let dl = displayLink else { return }

        // Pass a retained reference to the engine as the callback context.
        let engPtr = Unmanaged.passRetained(engine).toOpaque()

        CVDisplayLinkSetOutputCallback(dl, {
            (_, _, _, _, _, context) -> CVReturn in
            guard let ctx = context else { return kCVReturnSuccess }
            let eng = Unmanaged<EngineBridge>.fromOpaque(ctx).takeUnretainedValue()
            eng.renderFrame()
            return kCVReturnSuccess
        }, engPtr)

        CVDisplayLinkStart(dl)
    }

    private func stopDisplayLink() {
        guard let dl = displayLink else { return }
        CVDisplayLinkStop(dl)
        displayLink = nil
        // Balance the passRetained from startDisplayLink.
        Unmanaged.passUnretained(engine).release()
    }

    func sync(to frame: CGRect) {
        window.setFrame(frame, display: true)
        metalLayer.frame = CGRect(origin: .zero, size: frame.size)
        let scale = metalLayer.contentsScale
        engine.resize(width: UInt32(frame.width * scale), height: UInt32(frame.height * scale))
    }
}

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
    private var displayLinkContext: UnsafeMutableRawPointer?
    private var displayLinkDisplayID: CGDirectDisplayID?
    private var lastDrawablePixelSize: CGSize = .zero
    private let resizePixelTolerance: CGFloat = 2.0

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
        metalLayer.displaySyncEnabled = false
        metalLayer.maximumDrawableCount = 2
        metalLayer.presentsWithTransaction = false
        metalLayer.allowsNextDrawableTimeout = false
        metalLayer.frame              = CGRect(origin: .zero, size: frame.size)
        metalLayer.contentsScale      = window.screen?.backingScaleFactor ?? NSScreen.main?.backingScaleFactor ?? 2.0
        lastDrawablePixelSize         = drawablePixelSize(for: frame.size, scale: metalLayer.contentsScale)
        metalLayer.drawableSize       = lastDrawablePixelSize
        window.contentView?.wantsLayer = true
        window.contentView?.layer      = metalLayer

        engine.configure(layer: metalLayer)
    }

    func show() {
        window.orderFrontRegardless()
        startDisplayLink(displayID: currentDisplayID())
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

    private func startDisplayLink(displayID: CGDirectDisplayID? = nil) {
        stopDisplayLink()

        guard CVDisplayLinkCreateWithActiveCGDisplays(&displayLink) == kCVReturnSuccess,
              let dl = displayLink else { return }

        if let displayID = displayID ?? currentDisplayID() {
            CVDisplayLinkSetCurrentCGDisplay(dl, displayID)
            displayLinkDisplayID = displayID
        } else {
            displayLinkDisplayID = nil
        }

        let period = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(dl)
        if period.timeScale > 0, period.timeValue > 0 {
            engine.setDisplayRefreshPeriod(Double(period.timeValue) / Double(period.timeScale))
        }

        // Pass a retained reference to the engine as the callback context.
        let engPtr = Unmanaged.passRetained(engine).toOpaque()
        displayLinkContext = engPtr

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
        displayLinkDisplayID = nil
        if let context = displayLinkContext {
            // Balance the passRetained from startDisplayLink.
            Unmanaged<EngineBridge>.fromOpaque(context).release()
            displayLinkContext = nil
        }
    }

    func sync(to frame: CGRect) {
        window.setFrame(frame, display: true)
        metalLayer.frame = CGRect(origin: .zero, size: frame.size)
        let scale = window.screen?.backingScaleFactor ?? metalLayer.contentsScale
        metalLayer.contentsScale = scale
        let pixelSize = drawablePixelSize(for: frame.size, scale: scale)
        if shouldResize(to: pixelSize) {
            lastDrawablePixelSize = pixelSize
            metalLayer.drawableSize = pixelSize
            engine.resize(width: UInt32(max(CGFloat(1), pixelSize.width)),
                          height: UInt32(max(CGFloat(1), pixelSize.height)))
        }

        let displayID = currentDisplayID()
        if displayLink == nil || displayID != displayLinkDisplayID {
            startDisplayLink(displayID: displayID)
        }
    }

    private func currentDisplayID() -> CGDirectDisplayID? {
        guard let screenNumber = window.screen?.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber else {
            return nil
        }
        return CGDirectDisplayID(screenNumber.uint32Value)
    }

    private func drawablePixelSize(for size: CGSize, scale: CGFloat) -> CGSize {
        CGSize(width: (size.width * scale).rounded(.toNearestOrAwayFromZero),
               height: (size.height * scale).rounded(.toNearestOrAwayFromZero))
    }

    private func shouldResize(to pixelSize: CGSize) -> Bool {
        guard lastDrawablePixelSize.width > 0, lastDrawablePixelSize.height > 0 else { return true }
        return abs(pixelSize.width - lastDrawablePixelSize.width) >= resizePixelTolerance ||
               abs(pixelSize.height - lastDrawablePixelSize.height) >= resizePixelTolerance
    }
}

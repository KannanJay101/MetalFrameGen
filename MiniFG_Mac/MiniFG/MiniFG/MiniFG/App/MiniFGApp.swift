import SwiftUI
import ScreenCaptureKit
import CoreGraphics

@main
struct MiniFGApp: App {
    @StateObject private var appState = AppState()

    var body: some Scene {
        WindowGroup {
            MainView()
                .environmentObject(appState)
        }
        .defaultSize(width: 420, height: 520)
    }
}

// MARK: - MainView

struct MainView: View {
    @EnvironmentObject var appState: AppState

    var body: some View {
        VStack(spacing: 16) {
            // Header
            HStack {
                Image(systemName: "square.2.layers.3d.top.filled")
                    .font(.title)
                    .foregroundStyle(.blue)
                VStack(alignment: .leading) {
                    Text("MiniFG")
                        .font(.title2.bold())
                    Text("Frame Generator for macOS")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
            }
            .padding(.bottom, 4)

            Divider()

            if appState.isRunning {
                RunningView()
            } else {
                SetupView()
            }

            Spacer()

            // Status bar
            HStack {
                Circle()
                    .fill(appState.isRunning ? .green : .gray)
                    .frame(width: 8, height: 8)
                Text(appState.isRunning
                     ? "Capturing: \(appState.targetAppName)"
                     : "Idle")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                if !appState.errorMessage.isEmpty {
                    Image(systemName: "exclamationmark.triangle.fill")
                        .foregroundStyle(.yellow)
                    Text(appState.errorMessage)
                        .font(.caption)
                        .foregroundStyle(.red)
                        .lineLimit(1)
                }
            }
        }
        .padding()
        .frame(minWidth: 380, minHeight: 400)
        .onAppear {
            appState.refreshWindows()
        }
    }
}

// MARK: - SetupView

struct SetupView: View {
    @EnvironmentObject var appState: AppState
    @State private var targetApp: String = ""

    var body: some View {
        VStack(spacing: 12) {
            // Manual entry
            HStack {
                TextField("Target app name (e.g. Safari, Chess)", text: $targetApp)
                    .textFieldStyle(.roundedBorder)
                Button("Start") {
                    appState.startCapture(targetApp: targetApp)
                }
                .disabled(targetApp.isEmpty)
                .buttonStyle(.borderedProminent)
            }

            Divider()

            // Available windows list
            HStack {
                Text("Available Windows")
                    .font(.headline)
                Spacer()
                Button {
                    appState.refreshWindows()
                } label: {
                    Image(systemName: "arrow.clockwise")
                }
                .buttonStyle(.borderless)
            }

            if appState.isLoadingWindows {
                ProgressView("Scanning for windows...")
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else if appState.availableWindows.isEmpty {
                VStack(spacing: 8) {
                    Image(systemName: "macwindow.on.rectangle")
                        .font(.largeTitle)
                        .foregroundStyle(.secondary)
                    Text("No capturable windows found")
                        .foregroundStyle(.secondary)
                    Text("Make sure Screen Recording permission is granted\nin System Settings → Privacy & Security")
                        .font(.caption)
                        .foregroundStyle(.tertiary)
                        .multilineTextAlignment(.center)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollView {
                    LazyVStack(spacing: 4) {
                        ForEach(appState.availableWindows) { entry in
                            Button {
                                appState.startCapture(window: entry)
                            } label: {
                                HStack {
                                    Image(systemName: "macwindow")
                                    VStack(alignment: .leading, spacing: 1) {
                                        Text(entry.displayTitle)
                                            .lineLimit(1)
                                        Text(entry.sizeString)
                                            .font(.caption2)
                                            .foregroundStyle(.secondary)
                                    }
                                    Spacer()
                                    Image(systemName: "play.circle")
                                        .foregroundStyle(.blue)
                                }
                                .padding(.horizontal, 8)
                                .padding(.vertical, 6)
                                .contentShape(Rectangle())
                            }
                            .buttonStyle(.plain)
                            .background(
                                RoundedRectangle(cornerRadius: 6)
                                    .fill(Color.primary.opacity(0.05))
                            )
                        }
                    }
                }
            }
        }
    }
}

// MARK: - RunningView

struct RunningView: View {
    @EnvironmentObject var appState: AppState

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "record.circle")
                .font(.system(size: 48))
                .foregroundStyle(.green)
                .symbolEffect(.pulse)

            Text("Capturing \(appState.targetAppName)")
                .font(.title3.bold())

            // Live stats panel
            GroupBox("Pipeline Stats") {
                VStack(alignment: .leading, spacing: 6) {
                    StatRow(label: "Capture FPS", value: String(format: "%.1f", appState.captureFPS))
                    StatRow(label: "Output FPS", value: String(format: "%.1f", appState.outputFPS))
                    StatRow(label: "Frames Captured", value: "\(appState.capturedFrames)")
                    StatRow(label: "Frames Rendered", value: "\(appState.renderedFrames)")
                    StatRow(label: "Interpolated", value: "\(appState.interpolatedFrames)")
                    StatRow(label: "Resolution",
                            value: appState.captureResolution.isEmpty ? "---" : appState.captureResolution)
                    StatRow(label: "Status",
                            value: appState.interpolatedFrames > 0
                                ? "Interpolating"
                                : appState.capturedFrames > 0 ? "Receiving frames" : "Waiting for frames...")
                }
                .padding(.vertical, 4)
            }

            Text("Press Esc to stop capture")
                .font(.caption)
                .foregroundStyle(.secondary)
                .padding(.top, 2)

            Button("Stop Capture") {
                appState.stop()
            }
            .buttonStyle(.borderedProminent)
            .tint(.red)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

struct StatRow: View {
    let label: String
    let value: String

    var body: some View {
        HStack {
            Text(label)
                .foregroundStyle(.secondary)
                .frame(width: 130, alignment: .leading)
            Text(value)
                .fontDesign(.monospaced)
                .bold()
            Spacer()
        }
        .font(.callout)
    }
}

// MARK: - AppState

/// One capturable window in the picker list.
struct WindowEntry: Identifiable, Hashable {
    let id: CGWindowID
    let appName: String
    let title: String
    let size: CGSize

    var displayTitle: String {
        title.isEmpty ? appName : "\(appName) — \(title)"
    }

    var sizeString: String {
        "\(Int(size.width)) × \(Int(size.height))"
    }
}

@MainActor
final class AppState: ObservableObject {
    @Published var isRunning = false
    @Published var targetAppName: String = ""
    @Published var errorMessage: String = ""
    @Published var availableWindows: [WindowEntry] = []
    @Published var isLoadingWindows = false

    // Live stats (updated by timer)
    @Published var captureFPS: Double = 0
    @Published var outputFPS: Double = 0
    @Published var capturedFrames: Int = 0
    @Published var renderedFrames: Int = 0
    @Published var interpolatedFrames: Int = 0
    @Published var captureResolution: String = ""

    private var overlay: OverlayWindowController?
    private var stream: StreamManager?
    private var tracker = WindowTracker()
    private var statsTimer: Timer?
    private var trackingTimer: Timer?
    private var targetWindowID: CGWindowID = 0
    private var lastTrackedFrame: CGRect = .zero
    private var prevRenderCount: UInt64 = 0
    private var lastOutputFPSSample: CFAbsoluteTime = 0

    func refreshWindows() {
        isLoadingWindows = true
        errorMessage = ""

        // Prompt for Screen Recording permission if not already granted.
        if !CGPreflightScreenCaptureAccess() {
            CGRequestScreenCaptureAccess()
        }

        Task {
            do {
                // onScreenWindowsOnly: false — so fullscreen games are included.
                let content = try await SCShareableContent.excludingDesktopWindows(
                    false, onScreenWindowsOnly: false
                )
                let ownBundle = Bundle.main.bundleIdentifier
                let entries = content.windows.compactMap { window -> WindowEntry? in
                    guard let app = window.owningApplication,
                          !app.applicationName.isEmpty,
                          window.frame.width > 100,
                          window.frame.height > 100 else { return nil }
                    // Exclude MiniFG's own windows (the setup UI + the overlay).
                    if let ownBundle, app.bundleIdentifier == ownBundle { return nil }
                    return WindowEntry(
                        id: window.windowID,
                        appName: app.applicationName,
                        title: window.title ?? "",
                        size: window.frame.size
                    )
                }
                // Largest windows first — games are almost always the biggest.
                availableWindows = entries.sorted {
                    $0.size.width * $0.size.height > $1.size.width * $1.size.height
                }

                if availableWindows.isEmpty && !CGPreflightScreenCaptureAccess() {
                    errorMessage = "Screen Recording permission required — grant it in System Settings → Privacy & Security"
                }
            } catch {
                errorMessage = error.localizedDescription
                availableWindows = []
            }
            isLoadingWindows = false
        }
    }

    func startCapture(targetApp: String) {
        errorMessage = ""
        Task {
            do {
                guard let window = try await tracker.findWindow(for: targetApp) else {
                    throw MiniFGError.windowNotFound(targetApp)
                }
                try await start(window: window, displayName: targetApp)
            } catch {
                errorMessage = error.localizedDescription
                print("[MiniFG] \(error.localizedDescription)")
            }
        }
    }

    func startCapture(window entry: WindowEntry) {
        errorMessage = ""
        Task {
            do {
                guard let window = try await tracker.findWindow(id: entry.id) else {
                    throw MiniFGError.windowNotFound(entry.displayTitle)
                }
                try await start(window: window, displayName: entry.displayTitle)
            } catch {
                errorMessage = error.localizedDescription
                print("[MiniFG] \(error.localizedDescription)")
            }
        }
    }

    private func start(window: SCWindow, displayName: String) async throws {
        guard !isRunning else { return }

        self.targetWindowID = window.windowID

        let overlayCtrl = OverlayWindowController(frame: window.frame)
        self.overlay = overlayCtrl
        self.lastTrackedFrame = window.frame

        // Wire up the escape hotkey to stop capture
        overlayCtrl.onExitHotkey = { [weak self] in
            self?.stop()
        }

        overlayCtrl.show()

        let mgr = StreamManager()
        self.stream = mgr
        try await mgr.start(window: window) { [weak overlayCtrl] buffer, width, height in
            overlayCtrl?.engine.submitFrame(buffer: buffer, width: width, height: height)
        }

        targetAppName = displayName
        isRunning = true
        prevRenderCount = 0
        lastOutputFPSSample = CFAbsoluteTimeGetCurrent()

        // Start polling stats every 0.5s
        statsTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in
                self?.updateStats()
            }
        }

        // Track the target window position/size at ~10 Hz so the overlay
        // follows moves, resizes, and full-screen transitions.
        trackingTimer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in
                self?.trackTargetWindow()
            }
        }
    }

    private func trackTargetWindow() {
        guard let overlay = overlay, targetWindowID != 0 else { return }
        guard let frame = tracker.currentFrame(for: targetWindowID) else {
            // Target window disappeared — game may have closed.
            return
        }
        guard frame != lastTrackedFrame else { return }
        lastTrackedFrame = frame
        overlay.sync(to: frame)
    }

    private func updateStats() {
        guard let stats = stream?.stats else { return }
        captureFPS = stats.sampleFPS()
        capturedFrames = stats.capturedFrames

        let currentRender = overlay?.engine.renderCount ?? 0
        renderedFrames = Int(currentRender)
        interpolatedFrames = Int(overlay?.engine.interpCount ?? 0)

        // Compute output FPS from render count delta
        let now = CFAbsoluteTimeGetCurrent()
        let elapsed = now - lastOutputFPSSample
        if elapsed > 0.001 {
            let delta = currentRender - prevRenderCount
            outputFPS = Double(delta) / elapsed
            prevRenderCount = currentRender
            lastOutputFPSSample = now
        }

        let w = stats.lastCaptureWidth
        let h = stats.lastCaptureHeight
        captureResolution = w > 0 ? "\(w) x \(h)" : ""
    }

    func stop() {
        trackingTimer?.invalidate()
        trackingTimer = nil
        statsTimer?.invalidate()
        statsTimer = nil
        stream?.stop()
        overlay?.close()
        stream = nil
        overlay = nil
        targetWindowID = 0
        lastTrackedFrame = .zero
        isRunning = false
        captureFPS = 0
        outputFPS = 0
        capturedFrames = 0
        renderedFrames = 0
        interpolatedFrames = 0
        captureResolution = ""
        errorMessage = ""
    }
}

enum MiniFGError: LocalizedError {
    case windowNotFound(String)
    var errorDescription: String? {
        switch self {
        case .windowNotFound(let name):
            return "Could not find a window for \"\(name)\". Make sure the app is running."
        }
    }
}

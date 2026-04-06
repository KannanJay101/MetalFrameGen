import SwiftUI
import ScreenCaptureKit

@main
struct MiniFGApp: App {
    @StateObject private var appState = AppState()

    var body: some Scene {
        WindowGroup {
            MainView()
                .environmentObject(appState)
        }
        .defaultSize(width: 420, height: 500)
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
                        ForEach(appState.availableWindows, id: \.self) { name in
                            Button {
                                appState.startCapture(targetApp: name)
                            } label: {
                                HStack {
                                    Image(systemName: "macwindow")
                                    Text(name)
                                        .lineLimit(1)
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
                    StatRow(label: "Frames Captured", value: "\(appState.capturedFrames)")
                    StatRow(label: "Frames Rendered", value: "\(appState.renderedFrames)")
                    StatRow(label: "Resolution",
                            value: appState.captureResolution.isEmpty ? "—" : appState.captureResolution)
                    StatRow(label: "Status",
                            value: appState.capturedFrames > 0 ? "Receiving frames" : "Waiting for frames...")
                }
                .padding(.vertical, 4)
            }

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

@MainActor
final class AppState: ObservableObject {
    @Published var isRunning = false
    @Published var targetAppName: String = ""
    @Published var errorMessage: String = ""
    @Published var availableWindows: [String] = []
    @Published var isLoadingWindows = false

    // Live stats (updated by timer)
    @Published var captureFPS: Double = 0
    @Published var capturedFrames: Int = 0
    @Published var renderedFrames: Int = 0
    @Published var captureResolution: String = ""

    private var overlay: OverlayWindowController?
    private var stream: StreamManager?
    private var tracker = WindowTracker()
    private var statsTimer: Timer?

    func refreshWindows() {
        isLoadingWindows = true
        errorMessage = ""
        Task {
            do {
                let content = try await SCShareableContent.excludingDesktopWindows(
                    false, onScreenWindowsOnly: true
                )
                let names = content.windows.compactMap { window -> String? in
                    guard let app = window.owningApplication,
                          !app.applicationName.isEmpty,
                          window.frame.width > 100,
                          window.frame.height > 100 else { return nil }
                    return app.applicationName
                }
                // Deduplicate while preserving order
                var seen = Set<String>()
                availableWindows = names.filter { seen.insert($0).inserted }
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
                try await start(targetApp: targetApp)
            } catch {
                errorMessage = error.localizedDescription
                print("[MiniFG] \(error.localizedDescription)")
            }
        }
    }

    private func start(targetApp: String) async throws {
        guard !isRunning else { return }

        guard let window = try await tracker.findWindow(for: targetApp) else {
            throw MiniFGError.windowNotFound(targetApp)
        }

        let overlayCtrl = OverlayWindowController(frame: window.frame)
        self.overlay = overlayCtrl
        overlayCtrl.show()

        let mgr = StreamManager()
        self.stream = mgr
        try await mgr.start(window: window) { [weak overlayCtrl] buffer, width, height in
            overlayCtrl?.engine.submitFrame(buffer: buffer, width: width, height: height)
        }

        targetAppName = targetApp
        isRunning = true

        // Start polling stats every 0.5s
        statsTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            Task { @MainActor [weak self] in
                self?.updateStats()
            }
        }
    }

    private func updateStats() {
        guard let stats = stream?.stats else { return }
        captureFPS = stats.sampleFPS()
        capturedFrames = stats.capturedFrames
        renderedFrames = Int(overlay?.engine.renderCount ?? 0)
        let w = stats.lastCaptureWidth
        let h = stats.lastCaptureHeight
        captureResolution = w > 0 ? "\(w) x \(h)" : ""
    }

    func stop() {
        statsTimer?.invalidate()
        statsTimer = nil
        stream?.stop()
        overlay?.close()
        stream = nil
        overlay = nil
        isRunning = false
        captureFPS = 0
        capturedFrames = 0
        renderedFrames = 0
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

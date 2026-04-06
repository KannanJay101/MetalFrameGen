import ScreenCaptureKit
import CoreGraphics

/// Finds SCWindow instances matching a target application name.
final class WindowTracker {

    /// Returns the first on-screen window whose owning application display name
    /// matches `appName` (case-insensitive prefix match).
    func findWindow(for appName: String) async throws -> SCWindow? {
        let content = try await SCShareableContent.excludingDesktopWindows(
            false,
            onScreenWindowsOnly: true
        )

        return content.windows.first { window in
            guard let app = window.owningApplication else { return false }
            return app.applicationName
                .lowercased()
                .hasPrefix(appName.lowercased())
        }
    }

    /// Returns all capturable windows for a given bundle identifier.
    func windows(forBundleID bundleID: String) async throws -> [SCWindow] {
        let content = try await SCShareableContent.excludingDesktopWindows(
            false,
            onScreenWindowsOnly: true
        )
        return content.windows.filter {
            $0.owningApplication?.bundleIdentifier == bundleID
        }
    }
}

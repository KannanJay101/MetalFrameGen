import ScreenCaptureKit
import CoreGraphics
import AppKit

/// Finds SCWindow instances matching a target application name.
final class WindowTracker {

    /// Returns the LARGEST on-screen window whose owning application display
    /// name matches `appName` (case-insensitive prefix match). Picking the
    /// largest matches the intuition that the game window is the biggest one
    /// (e.g. Steam's library window is smaller than a running game).
    func findWindow(for appName: String) async throws -> SCWindow? {
        let content = try await SCShareableContent.excludingDesktopWindows(
            false,
            onScreenWindowsOnly: false
        )
        let needle = appName.lowercased()
        let matches = content.windows.filter { window in
            guard let app = window.owningApplication else { return false }
            return app.applicationName.lowercased().hasPrefix(needle)
        }
        return matches.max(by: { a, b in
            a.frame.width * a.frame.height < b.frame.width * b.frame.height
        })
    }

    /// Looks up a specific window by its CGWindowID.
    func findWindow(id: CGWindowID) async throws -> SCWindow? {
        let content = try await SCShareableContent.excludingDesktopWindows(
            false,
            onScreenWindowsOnly: false
        )
        return content.windows.first { $0.windowID == id }
    }

    /// Returns all capturable windows for a given bundle identifier.
    func windows(forBundleID bundleID: String) async throws -> [SCWindow] {
        let content = try await SCShareableContent.excludingDesktopWindows(
            false,
            onScreenWindowsOnly: false
        )
        return content.windows.filter {
            $0.owningApplication?.bundleIdentifier == bundleID
        }
    }

    /// Returns the current frame (in AppKit coordinates) of a window by its
    /// CGWindowID, or nil if the window no longer exists.
    func currentFrame(for windowID: CGWindowID) -> CGRect? {
        guard let list = CGWindowListCopyWindowInfo(.optionIncludingWindow, windowID)
                as? [[String: Any]],
              let info = list.first,
              let boundsAny = info[kCGWindowBounds as String] else {
            return nil
        }
        let cfDict = boundsAny as! CFDictionary
        var cgRect = CGRect.zero
        guard CGRectMakeWithDictionaryRepresentation(cfDict, &cgRect) else { return nil }

        // CG origin = top-left of primary display; AppKit origin = bottom-left.
        let primaryHeight = NSScreen.screens.first?.frame.height ?? 0
        return CGRect(x: cgRect.origin.x,
                      y: primaryHeight - cgRect.origin.y - cgRect.height,
                      width: cgRect.width,
                      height: cgRect.height)
    }
}

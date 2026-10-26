import AppKit

/// The screen saver, when one is running (started by a hot corner, idle time or `open -a ScreenSaverEngine`).
/// Unlocking from the cube stops it first: the first key typed into a running saver only wakes it, so the
/// password would arrive one character short.
enum RunningSaver {
    private static let engineID = "com.apple.ScreenSaver.Engine"

    /// Stops a running screen saver; true when one was running.
    @discardableResult
    static func stop() -> Bool {
        let apps = NSRunningApplication.runningApplications(withBundleIdentifier: engineID)
        apps.forEach { $0.terminate() }
        return !apps.isEmpty
    }
}

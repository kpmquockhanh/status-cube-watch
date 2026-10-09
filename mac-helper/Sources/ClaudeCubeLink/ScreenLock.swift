import CoreGraphics
import Foundation

/// Whether the Mac's screen is locked, from the session's own notifications. The cube shows its
/// unlock prompt only while this says locked (and Unlock with cube is on).
final class ScreenLock {
    var onChange: (Bool) -> Void = { _ in }
    private(set) var isLocked = ScreenLock.readLocked()
    private var observers: [NSObjectProtocol] = []

    func start() {
        let center = DistributedNotificationCenter.default()
        for (name, locked) in [("com.apple.screenIsLocked", true), ("com.apple.screenIsUnlocked", false)] {
            observers.append(center.addObserver(forName: Notification.Name(name), object: nil, queue: .main) { [weak self] _ in
                self?.set(locked)
            })
        }
        set(Self.readLocked())  // in case it changed before the observers were in place
    }

    private func set(_ locked: Bool) {
        guard locked != isLocked else { return }
        isLocked = locked
        onChange(locked)
    }

    private static func readLocked() -> Bool {
        guard let d = CGSessionCopyCurrentDictionary() as? [String: Any] else { return false }
        return d["CGSSessionScreenIsLocked"] as? Bool ?? false
    }
}

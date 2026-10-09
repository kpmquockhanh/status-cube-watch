import Foundation

/// When a tap on the cube may unlock the Mac (Control `07`), and when the cube should offer it.
///
/// The cube only asks; the password stays on the Mac (Keychain) and is typed into the lock screen.
/// It is typed only while the screen is actually locked, the feature is on, a password is stored and
/// the app may post keystrokes, and at most once per `minGap`, so a repeated or replayed `07` cannot
/// type it twice or into anything but the lock screen.
public struct UnlockPolicy {
    public enum Decision: Equatable {
        case type
        case ignore(String)
    }

    public let minGap: TimeInterval
    private var lastTyped: Date?

    public init(minGap: TimeInterval = 5) { self.minGap = minGap }

    /// Whether the Volume frame tells the cube to show its prompt (`MacVolume.locked`).
    public static func offer(enabled: Bool, locked: Bool) -> Bool { enabled && locked }

    public mutating func decide(enabled: Bool, locked: Bool, trusted: Bool, now: Date) -> Decision {
        guard enabled else { return .ignore("Unlock with cube is off") }
        guard locked else { return .ignore("the screen is not locked") }
        guard trusted else {
            return .ignore("no Accessibility permission: allow Claude Cube Link in System Settings > "
                + "Privacy & Security > Accessibility")
        }
        if let last = lastTyped, now.timeIntervalSince(last) < minGap { return .ignore("an unlock was just typed") }
        lastTyped = now
        return .type
    }
}

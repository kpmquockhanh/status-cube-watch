import Foundation

/// When to write to the cube: on change, on the 5 s heartbeat, or at once when
/// the cube asks. Never when the bridge gave nothing, so a dead bridge shows
/// up on the cube as stale data instead of being papered over.
public struct PushPolicy {
    public let heartbeat: TimeInterval
    /// Ticks come from a fixed timer but `now` is taken after an async fetch, so
    /// the gap can read a little under the heartbeat. Without slack an unchanged
    /// body would skip that tick and the real gap would double.
    private let tolerance: TimeInterval = 0.5
    private var lastSent: Date?
    private var lastBody: Data?

    public init(heartbeat: TimeInterval = 5) { self.heartbeat = heartbeat }

    public func shouldSend(body: Data?, now: Date, force: Bool) -> Bool {
        guard let body else { return false }
        if force { return true }
        guard let lastSent, let lastBody else { return true }
        return body != lastBody || now.timeIntervalSince(lastSent) >= heartbeat - tolerance
    }

    public mutating func didSend(body: Data, at: Date) {
        lastBody = body
        lastSent = at
    }
}

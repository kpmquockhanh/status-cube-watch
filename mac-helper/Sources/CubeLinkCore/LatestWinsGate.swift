import Foundation

/// Single write in flight, latest pending wins: Volume writes arrive faster than a
/// write-with-response drains, and only the newest state matters.
public struct LatestWinsGate {
    private var inFlight = false
    private var pending: Data?

    public init() {}

    /// What to send now: `d` when nothing is in flight, else nil (`d` is kept as the pending one).
    public mutating func offer(_ d: Data) -> Data? {
        if inFlight {
            pending = d
            return nil
        }
        inFlight = true
        return d
    }

    /// The write finished (or failed). Returns the pending data to send next, if any.
    public mutating func completed() -> Data? {
        if let next = pending {
            pending = nil
            inFlight = true
            return next
        }
        inFlight = false
        return nil
    }

    public mutating func reset() {
        inFlight = false
        pending = nil
    }
}

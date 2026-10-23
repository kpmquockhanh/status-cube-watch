import Foundation

/// The cube's Bluetooth identity and the checks the iPhone's link makes (`docs/ble-protocol.md`).
public enum CubeBLE {
    public static let service = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a01"
    public static let info = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a04"
    public static let nav = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a07"
    public static let protocolVersion: UInt8 = 1
    /// First firmware with the Nav characteristic and the second (iPhone) bond.
    public static let minNavFwRev: UInt8 = 8

    /// Info is `[proto_ver, fw_rev]`. True when that cube can take Nav frames from this app.
    public static func infoSupportsNav(_ info: [UInt8]) -> Bool {
        info.count >= 2 && info[0] == protocolVersion && info[1] >= minNavFwRev
    }
}

/// Outgoing Nav chunks for one link, "latest wins" without ever interleaving two frames.
///
/// A frame being sent always goes out whole (its chunks share one seq, in order). A frame offered
/// meanwhile waits; a newer one replaces it before it has started. A frame gets its seq (+1, wrapping)
/// only when it starts, so the cube never sees a seq repeat or two frames' chunks mixed.
public struct NavChunkQueue: Sendable {
    public enum Offer: Equatable, Sendable {
        /// Nothing was going out: the frame is now the current one.
        case started
        /// Waiting behind the current frame (a waiting frame it replaced is dropped).
        case queued
        /// More than 32 chunks or over 8192 bytes: the cube would reject it, so it is dropped.
        case tooLarge
    }

    public private(set) var seq: UInt8 = 0
    private var current: [Data] = []
    private var currentIndex = 0
    private var pending: (frame: Data, maxWrite: Int)?

    public init() {}

    /// True when no chunk is waiting to be written.
    public var isIdle: Bool { currentIndex >= current.count && pending == nil }

    public mutating func offer(_ frame: Data, maxWrite: Int) -> Offer {
        guard frame.count <= NavLimits.maxFrameBytes, frame.count <= chunkCapacity(maxWrite: maxWrite) else {
            return .tooLarge
        }
        if currentIndex >= current.count {
            start(frame, maxWrite: maxWrite)
            return .started
        }
        pending = (frame, maxWrite)
        return .queued
    }

    /// The next chunk to write, or nil when there is none. Moves on to the waiting frame once
    /// the current one is all out.
    public mutating func next() -> Data? {
        if currentIndex >= current.count {
            guard let p = pending else { return nil }
            pending = nil
            start(p.frame, maxWrite: p.maxWrite)
        }
        defer { currentIndex += 1 }
        return current[currentIndex]
    }

    /// A new connection: the cube resets its frame state, so does the phone.
    public mutating func reset() {
        seq = 0
        current = []
        currentIndex = 0
        pending = nil
    }

    private mutating func start(_ frame: Data, maxWrite: Int) {
        seq &+= 1
        current = chunks(frame, seq: seq, maxWrite: maxWrite)
        currentIndex = 0
    }
}

extension NavScene {
    /// What the cube shows when no ride is going: also the phone's first write on a new link.
    public static let ended = NavScene(state: .ended, turn: .straight, exit: 0, dir: 0, gpsWeak: false,
                                       dist: "", street: "", eta: "", route: [], streets: [])
}

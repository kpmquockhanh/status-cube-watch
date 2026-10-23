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
            pending = nil  // an older frame parked behind the one just finished must not follow this one
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

/// What the iPhone's link does after it loses (or drops) a cube.
public enum NavLinkDrop: Equatable, Sendable {
    /// A working link went away (out of range, cube rebooted): connect again at once. iOS keeps
    /// the pending connect even while the app is suspended.
    case reconnectNow
    /// The link never became ready, or setup failed: try the same cube again after a back-off,
    /// so a refused pairing does not bring the passkey dialog straight back.
    case retryLater
    /// A cube found by scanning that never became ready (a neighbour's cube, or one outside its
    /// pairing window): skip it for the rest of the session and scan again.
    case skipAndScan
    /// The cube deleted this iPhone's bond: only the user can fix it (forget "Claude Cube" in
    /// Settings > Bluetooth), so stop retrying that cube.
    case needsForget
}

public enum NavLinkPolicy {
    /// `wasReady`: the link reached `connected`. `remembered`: the cube is the one this app paired
    /// with before. `tooOld`: its firmware cannot take Nav frames (retry slowly, it may be updated).
    public static func onDrop(wasReady: Bool, remembered: Bool, tooOld: Bool,
                              peerRemovedPairing: Bool) -> NavLinkDrop {
        if peerRemovedPairing { return .needsForget }
        if wasReady { return .reconnectNow }
        if !remembered && !tooOld { return .skipAndScan }
        return .retryLater
    }

    /// The last scene older than this is not resent on reconnect.
    public static let resendMaxAge: TimeInterval = 2

    /// The first frame on a link: `.ended` on a first pairing (it identifies the iPhone to the
    /// cube at once) or when the last scene is missing or stale, else the last scene.
    public static func firstFrame(firstPairing: Bool, last: NavScene?, age: TimeInterval) -> NavScene {
        guard !firstPairing, let last, age <= resendMaxAge else { return .ended }
        return last
    }
}

/// Cubes skipped after they never became ready (`NavLinkDrop.skipAndScan`). An entry expires after
/// `ttl`, so the user's own cube on its first pairing (wrong passkey, cancelled dialog) gets
/// another try without the app having to be reopened.
public struct NavSkipList: Sendable {
    public static let ttl: TimeInterval = 60
    private var since: [UUID: Date] = [:]

    public init() {}

    public mutating func skip(_ id: UUID, at now: Date) { since[id] = now }

    public func contains(_ id: UUID, now: Date) -> Bool {
        guard let t = since[id] else { return false }
        return now.timeIntervalSince(t) < Self.ttl
    }

    public mutating func removeAll() { since.removeAll() }
}

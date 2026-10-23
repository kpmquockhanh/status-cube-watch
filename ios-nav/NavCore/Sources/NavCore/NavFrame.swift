import Foundation

public enum NavState: UInt8, Sendable { case ended, navigating, rerouting, offRouteNoSignal, arrived }

public struct ScreenPoint: Equatable, Sendable {
    public var x: Int16
    public var y: Int16
    public init(x: Int16, y: Int16) { self.x = x; self.y = y }
}

public struct SceneStreet: Equatable, Sendable {
    public var cls: UInt8
    public var points: [ScreenPoint]
    public init(cls: UInt8, points: [ScreenPoint]) { self.cls = cls; self.points = points }
}

/// One ready-to-draw map frame for the cube, in screen pixels of its 240x280 panel.
public struct NavScene: Equatable, Sendable {
    public var state: NavState
    public var turn: NavTurn
    public var exit: UInt8
    public var dir: UInt8
    public var gpsWeak: Bool
    public var dist: String
    public var street: String
    public var eta: String
    public var route: [ScreenPoint]
    public var streets: [SceneStreet]
    public init(state: NavState, turn: NavTurn, exit: UInt8, dir: UInt8, gpsWeak: Bool,
                dist: String, street: String, eta: String, route: [ScreenPoint], streets: [SceneStreet]) {
        self.state = state; self.turn = turn; self.exit = exit; self.dir = dir; self.gpsWeak = gpsWeak
        self.dist = dist; self.street = street; self.eta = eta; self.route = route; self.streets = streets
    }
}

/// Wire limits of the cube's decoder (`firmware/src/nav_frame.h`).
public enum NavLimits {
    public static let maxDist = 15, maxStreet = 63, maxETA = 23
    public static let maxRoute = 256, maxStreets = 96, maxStreetPoints = 640
    public static let maxFrameBytes = 8192, maxChunks = 32
    public static let minX = -64, maxX = 304, minY = -64, maxY = 344
}

/// NFC-normalised UTF-8, cut to at most `limit` bytes on a character boundary.
func wireString(_ s: String, limit: Int) -> [UInt8] {
    var chars = Array(s.precomposedStringWithCanonicalMapping)
    var bytes = Array(String(chars).utf8)
    while bytes.count > limit, !chars.isEmpty {
        chars.removeLast()
        bytes = Array(String(chars).utf8)
    }
    return bytes
}

/// Plan 2 layout, little-endian. Callers keep route/street counts within `NavLimits`.
public func encode(_ scene: NavScene) -> Data {
    var d = Data()
    func u8(_ v: UInt8) { d.append(v) }
    func u16(_ v: Int) { d.append(UInt8(v & 0xff)); d.append(UInt8((v >> 8) & 0xff)) }
    func i16(_ v: Int16) { u16(Int(UInt16(bitPattern: v))) }
    func str(_ s: String, _ limit: Int) { let b = wireString(s, limit: limit); u8(UInt8(b.count)); d.append(contentsOf: b) }
    u8(1)
    u8(scene.state.rawValue); u8(scene.turn.rawValue); u8(scene.exit); u8(scene.dir)
    u8(scene.gpsWeak ? 1 : 0)
    str(scene.dist, NavLimits.maxDist)
    str(scene.street, NavLimits.maxStreet)
    str(scene.eta, NavLimits.maxETA)
    u16(scene.route.count)
    for p in scene.route { i16(p.x); i16(p.y) }
    u8(UInt8(scene.streets.count))
    for s in scene.streets {
        u8(s.cls); u8(UInt8(s.points.count))
        for p in s.points { i16(p.x); i16(p.y) }
    }
    return d
}

/// Largest frame deliverable in at most 32 chunks of `maxWrite - 4` body bytes.
public func chunkCapacity(maxWrite: Int) -> Int {
    min(NavLimits.maxFrameBytes, NavLimits.maxChunks * (maxWrite - 4))
}

/// `[ver=1][seq][idx][total]` + body, body at most `maxWrite - 4` bytes.
public func chunks(_ frame: Data, seq: UInt8, maxWrite: Int) -> [Data] {
    let body = max(1, maxWrite - 4)
    let total = max(1, (frame.count + body - 1) / body)
    let bytes = [UInt8](frame)
    return (0..<total).map { i in
        var c = Data([1, seq, UInt8(i), UInt8(total)])
        c.append(contentsOf: bytes[(i * body)..<min(bytes.count, (i + 1) * body)])
        return c
    }
}

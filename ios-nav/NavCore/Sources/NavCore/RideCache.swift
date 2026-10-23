import Foundation

/// The last ride as the cache holds it: the route, its tiles and the destination the rider asked
/// for (not the route's snapped end).
public struct CachedRide: Sendable {
    public var route: Route
    public var tiles: [TileID: [StreetLine]]
    public var destination: Coord
}

/// Keeps the last ride (route + tiles + destination) on disk for an offline restart of the same
/// trip. Coordinates are stored as integer microdegrees. Writes go off the caller's thread, one
/// after another; `waitForPendingSave` lets a reader see the latest one.
public final class RideCache: @unchecked Sendable {
    /// Bumped whenever the file layout changes; a file of another version reads as no ride.
    static let version = 2

    private let file: URL
    private let lock = NSLock()
    private var pending: Task<Void, Never>?

    public init(directory: URL) {
        file = directory.appendingPathComponent("last-ride.json")
    }

    private struct Header: Decodable { var v: Int }

    private struct DTO: Codable {
        struct Instr: Codable {
            var turn: UInt8, exit: UInt8, street: String, distance: Double, time: Double, lo: Int, hi: Int
        }
        /// `p` = lat, lon, lat, lon, ... in microdegrees.
        struct Line: Codable { var c: UInt8; var p: [Int32] }
        struct Tile: Codable { var x: Int; var y: Int; var lines: [Line] }
        var v: Int
        var dest: [Int32]
        var pts: [Int32]
        var distance: Double, time: Double
        var instr: [Instr]
        var tiles: [Tile]
    }

    private static func pack(_ cs: [Coord]) -> [Int32] {
        var out: [Int32] = []
        out.reserveCapacity(cs.count * 2)
        for c in cs {
            out.append(Int32(clamping: Int((c.lat * 1e6).rounded())))
            out.append(Int32(clamping: Int((c.lon * 1e6).rounded())))
        }
        return out
    }

    private static func unpack(_ a: [Int32]) -> [Coord]? {
        guard a.count % 2 == 0 else { return nil }
        return stride(from: 0, to: a.count, by: 2).map { Coord(lat: Double(a[$0]) / 1e6, lon: Double(a[$0 + 1]) / 1e6) }
    }

    /// Writes now, on the calling thread.
    public func save(route: Route, tiles: [TileID: [StreetLine]], destination: Coord) {
        let dto = DTO(
            v: Self.version, dest: Self.pack([destination]), pts: Self.pack(route.points),
            distance: route.distance, time: route.time,
            instr: route.instructions.map {
                .init(turn: $0.turn.rawValue, exit: $0.exit, street: $0.streetName, distance: $0.distance,
                      time: $0.time, lo: $0.interval.lowerBound, hi: $0.interval.upperBound)
            },
            tiles: tiles.map { id, lines in .init(x: id.x, y: id.y, lines: lines.map { .init(c: $0.cls, p: Self.pack($0.points)) }) })
        guard let data = try? JSONEncoder().encode(dto) else { return }
        try? FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        try? data.write(to: file, options: .atomic)
    }

    /// Writes off the caller's thread, after any write still in flight.
    public func saveInBackground(route: Route, tiles: [TileID: [StreetLine]], destination: Coord) {
        lock.lock(); defer { lock.unlock() }
        let previous = pending
        pending = Task.detached(priority: .utility) { [self] in
            await previous?.value
            save(route: route, tiles: tiles, destination: destination)
        }
    }

    /// Returns once every write started so far is on disk.
    public func waitForPendingSave() async {
        await currentSave()?.value
    }

    private func currentSave() -> Task<Void, Never>? {
        lock.lock(); defer { lock.unlock() }
        return pending
    }

    public func loadLast() -> CachedRide? {
        guard let data = try? Data(contentsOf: file),
              let head = try? JSONDecoder().decode(Header.self, from: data), head.v == Self.version,
              let dto = try? JSONDecoder().decode(DTO.self, from: data),
              let points = Self.unpack(dto.pts), let dest = Self.unpack(dto.dest)?.first else { return nil }
        var instructions: [Instruction] = []
        for i in dto.instr {
            guard let turn = NavTurn(rawValue: i.turn), i.lo >= 0, i.lo <= i.hi, i.hi < points.count else { return nil }
            instructions.append(Instruction(turn: turn, exit: i.exit, streetName: i.street, distance: i.distance,
                                            time: i.time, interval: i.lo...i.hi))
        }
        var tiles: [TileID: [StreetLine]] = [:]
        for t in dto.tiles {
            var lines: [StreetLine] = []
            for l in t.lines {
                guard let pts = Self.unpack(l.p) else { return nil }
                lines.append(StreetLine(cls: l.c, points: pts))
            }
            tiles[TileID(x: t.x, y: t.y)] = lines
        }
        return CachedRide(route: Route(points: points, distance: dto.distance, time: dto.time, instructions: instructions),
                          tiles: tiles, destination: dest)
    }

    /// The cached ride's destination when it is within `meters` of `dest`.
    public func hasRide(near dest: Coord, within meters: Double = 100) async -> Bool {
        await waitForPendingSave()
        guard let r = loadLast() else { return false }
        return distanceMeters(r.destination, dest) <= meters
    }
}

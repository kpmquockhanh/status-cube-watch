import Foundation

/// Keeps the last ride (route + tiles) on disk for an offline restart of the same trip.
public struct RideCache {
    private let file: URL

    public init(directory: URL) {
        file = directory.appendingPathComponent("last-ride.json")
    }

    private struct DTO: Codable {
        struct Instr: Codable {
            var turn: UInt8, exit: UInt8, street: String, distance: Double, time: Double, lo: Int, hi: Int
        }
        struct Line: Codable { var cls: UInt8; var pts: [Coord] }
        struct Tile: Codable { var id: TileID; var lines: [Line] }
        var points: [Coord]
        var distance: Double, time: Double
        var instructions: [Instr]
        var tiles: [Tile]
    }

    public func save(route: Route, tiles: [TileID: [StreetLine]]) {
        let dto = DTO(
            points: route.points, distance: route.distance, time: route.time,
            instructions: route.instructions.map {
                .init(turn: $0.turn.rawValue, exit: $0.exit, street: $0.streetName, distance: $0.distance,
                      time: $0.time, lo: $0.interval.lowerBound, hi: $0.interval.upperBound)
            },
            tiles: tiles.map { id, lines in .init(id: id, lines: lines.map { .init(cls: $0.cls, pts: $0.points) }) })
        guard let data = try? JSONEncoder().encode(dto) else { return }
        try? FileManager.default.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
        try? data.write(to: file, options: .atomic)
    }

    public func loadLast() -> (Route, [TileID: [StreetLine]])? {
        guard let data = try? Data(contentsOf: file), let dto = try? JSONDecoder().decode(DTO.self, from: data) else { return nil }
        var instructions: [Instruction] = []
        for i in dto.instructions {
            guard let turn = NavTurn(rawValue: i.turn), i.lo >= 0, i.lo <= i.hi, i.hi < dto.points.count else { return nil }
            instructions.append(Instruction(turn: turn, exit: i.exit, streetName: i.street, distance: i.distance,
                                            time: i.time, interval: i.lo...i.hi))
        }
        var tiles: [TileID: [StreetLine]] = [:]
        for t in dto.tiles { tiles[t.id] = t.lines.map { StreetLine(cls: $0.cls, points: $0.pts) } }
        return (Route(points: dto.points, distance: dto.distance, time: dto.time, instructions: instructions), tiles)
    }
}

import Foundation

/// Raw values match the cube's turn numbering.
public enum NavTurn: UInt8, Sendable {
    case straight, slightLeft, left, sharpLeft, slightRight, right,
         sharpRight, uTurn, keepLeft, keepRight, roundabout, arrive, rejoin
}

public func navTurn(graphHopperSign sign: Int) -> NavTurn {
    switch sign {
    case -98, -8, 8: return .uTurn
    case -7: return .keepLeft
    case 7: return .keepRight
    case -3: return .sharpLeft
    case -2: return .left
    case -1: return .slightLeft
    case 0: return .straight
    case 1: return .slightRight
    case 2: return .right
    case 3: return .sharpRight
    case 4, 5: return .arrive
    case 6, -6: return .roundabout
    default: return .straight
    }
}

public struct Instruction: Equatable, Sendable {
    public var turn: NavTurn
    public var exit: UInt8
    public var streetName: String
    public var distance: Double
    public var time: Double
    /// Indices into `Route.points`.
    public var interval: ClosedRange<Int>
    public init(turn: NavTurn, exit: UInt8 = 0, streetName: String, distance: Double, time: Double, interval: ClosedRange<Int>) {
        self.turn = turn; self.exit = exit; self.streetName = streetName
        self.distance = distance; self.time = time; self.interval = interval
    }
}

public struct Route: Equatable, Sendable {
    public var points: [Coord]
    public var distance: Double
    public var time: Double
    public var instructions: [Instruction]
    public init(points: [Coord], distance: Double, time: Double, instructions: [Instruction]) {
        self.points = points; self.distance = distance; self.time = time; self.instructions = instructions
    }
}

public enum RouteDecoding {
    public struct Failure: Error, Equatable { public var reason: String }

    private struct Response: Decodable {
        struct Path: Decodable {
            struct Points: Decodable { var coordinates: [[Double]] }
            struct Instr: Decodable {
                var sign: Int
                var street_name: String?
                var distance: Double
                var time: Double
                var interval: [Int]
                var exit_number: Int?
            }
            var distance: Double
            var time: Double
            var points: Points
            var instructions: [Instr]?
        }
        var paths: [Path]
    }

    /// Decodes a GraphHopper route response (`points_encoded:false`, `instructions:true`).
    public static func decode(_ data: Data) throws -> Route {
        let resp = try JSONDecoder().decode(Response.self, from: data)
        guard let p = resp.paths.first else { throw Failure(reason: "no path") }
        var points: [Coord] = []
        points.reserveCapacity(p.points.coordinates.count)
        for c in p.points.coordinates {
            guard c.count >= 2 else { throw Failure(reason: "bad coordinate") }
            points.append(Coord(lat: c[1], lon: c[0]))
        }
        var out: [Instruction] = []
        for i in p.instructions ?? [] {
            guard i.interval.count == 2, i.interval[0] >= 0, i.interval[0] <= i.interval[1],
                  i.interval[1] < points.count else { throw Failure(reason: "bad interval") }
            let turn = navTurn(graphHopperSign: i.sign)
            let exit = turn == .roundabout ? UInt8(clamping: i.exit_number ?? 0) : 0
            out.append(Instruction(turn: turn, exit: exit, streetName: i.street_name ?? "",
                                   distance: i.distance, time: i.time,
                                   interval: i.interval[0]...i.interval[1]))
        }
        return Route(points: points, distance: p.distance, time: p.time, instructions: out)
    }
}

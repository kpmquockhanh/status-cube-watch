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
    /// Meters.
    public var distance: Double
    /// Seconds (GraphHopper reports milliseconds; converted in `RouteDecoding.decode`).
    public var time: Double
    /// Indices into `Route.points`.
    public var interval: ClosedRange<Int>
    public init(turn: NavTurn, exit: UInt8 = 0, streetName: String, distance: Double, time: Double, interval: ClosedRange<Int>) {
        self.turn = turn; self.exit = exit; self.streetName = streetName
        self.distance = distance; self.time = time; self.interval = interval
    }
}

/// What the route list shows about a route.
public struct RouteStats: Equatable, Sendable {
    /// Instructions that are a real turn (left, right, sharp, U-turn, roundabout).
    public var turns: Int
    /// Lit stretches along the route; nil when the server sent no signals detail.
    public var lights: Int?
    /// Share of the length on primary and trunk roads, 0...1.
    public var mainRoadShare: Double
    public init(turns: Int, lights: Int?, mainRoadShare: Double) {
        self.turns = turns; self.lights = lights; self.mainRoadShare = mainRoadShare
    }
}

public struct Route: Equatable, Sendable {
    public var points: [Coord]
    /// Meters.
    public var distance: Double
    /// Seconds (converted from GraphHopper milliseconds).
    public var time: Double
    public var instructions: [Instruction]
    public var stats: RouteStats
    public init(points: [Coord], distance: Double, time: Double, instructions: [Instruction],
                stats: RouteStats = RouteStats(turns: 0, lights: nil, mainRoadShare: 0)) {
        self.points = points; self.distance = distance; self.time = time; self.instructions = instructions
        self.stats = stats
    }
}

public enum RouteDecoding {
    public struct Failure: Error, Equatable { public var reason: String }

    /// Path detail that marks traffic lights (the server's `crossing` encoded value), and its value.
    public static let signalsDetail: String? = "crossing"
    public static let signalsValue = "traffic_signals"
    static let mainRoadClasses: Set<String> = ["primary", "trunk"]
    static let turnKinds: Set<NavTurn> = [.left, .right, .sharpLeft, .sharpRight, .uTurn, .roundabout]

    /// One path-detail interval `[from, to, value]`; a value that is not a string reads as "".
    struct Interval: Decodable {
        var from: Int, to: Int, value: String
        init(from decoder: Decoder) throws {
            var c = try decoder.unkeyedContainer()
            from = try c.decode(Int.self); to = try c.decode(Int.self)
            value = (try? c.decode(String.self)) ?? ""
        }
    }

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
            var details: [String: [Interval]]?
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
                                   distance: i.distance, time: i.time / 1000,
                                   interval: i.interval[0]...i.interval[1]))
        }
        let details = p.details ?? [:]
        for intervals in details.values {
            for i in intervals where i.from < 0 || i.from > i.to || i.to >= points.count {
                throw Failure(reason: "bad detail interval")
            }
        }
        var main = 0.0, total = 0.0
        for k in points.indices.dropFirst() { total += distanceMeters(points[k - 1], points[k]) }
        for i in details["road_class"] ?? [] where mainRoadClasses.contains(i.value.lowercased()) {
            for k in i.from..<i.to { main += distanceMeters(points[k], points[k + 1]) }
        }
        let lights = signalsDetail.flatMap { details[$0] }.map { intervals in
            intervals.filter { $0.value.lowercased() == signalsValue }.count
        }
        let stats = RouteStats(turns: out.filter { turnKinds.contains($0.turn) }.count, lights: lights,
                               mainRoadShare: total > 0 ? min(1, main / total) : 0)
        return Route(points: points, distance: p.distance, time: p.time / 1000, instructions: out, stats: stats)
    }
}

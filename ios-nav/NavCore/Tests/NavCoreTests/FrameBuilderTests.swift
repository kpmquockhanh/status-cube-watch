import Foundation
import Testing
@testable import NavCore
import struct NavCore.Progress

private let origin = Coord(lat: 10.0, lon: 106.0)
private let mPerDegLat = 111_194.9266
private func north(_ m: Double, east: Double = 0) -> Coord {
    Coord(lat: origin.lat + m / mPerDegLat, lon: origin.lon + east / (mPerDegLat * cos(origin.lat * .pi / 180)))
}

private func progress(heading: Double = 0, toTurn: Double = 500, snapped: Coord = origin, along: Double = 0) -> Progress {
    Progress(snapped: snapped, heading: heading, distanceAlong: along, nextInstruction: 0, distanceToTurn: toTurn,
             remainingTime: 720, offRoute: false, distanceToRoute: 0, bearingToRoute: 0, arrived: false, gpsWeak: false)
}

private func straightRoute(length: Double = 1000, step: Double = 10) -> Route {
    let pts = stride(from: 0.0, through: length, by: step).map { north($0) }
    return Route(points: pts, distance: length, time: 720, instructions: [
        Instruction(turn: .right, streetName: "Lê Lợi", distance: length, time: 720, interval: (pts.count - 1)...(pts.count - 1))])
}

private func allPoints(_ s: NavScene) -> [ScreenPoint] { s.route + s.streets.flatMap(\.points) }

@Suite struct FrameBuilderTests {
    @Test func zoom() {
        #expect(metersPerPixel(distanceToTurn: 150, speed: 5) == 0.8)
        #expect(metersPerPixel(distanceToTurn: 200, speed: 20) == 0.8)
        #expect(metersPerPixel(distanceToTurn: 500, speed: 5) == 1.5)
        #expect(metersPerPixel(distanceToTurn: 2500, speed: 11) == 1.5)
        #expect(metersPerPixel(distanceToTurn: 2500, speed: 12) == 3.0)
    }

    @Test func riderAndRoutePlacement() {
        let route = straightRoute(length: 100)
        let p = progress()
        let s = buildScene(progress: p, route: route, streets: [], state: .navigating)
        let mpp = metersPerPixel(distanceToTurn: 500, speed: 0)
        #expect(s.route.first == ScreenPoint(x: 120, y: 200))
        // A straight route simplifies to its two ends.
        #expect(s.route.last == ScreenPoint(x: 120, y: Int16((200 - 100 / mpp).rounded())))
        #expect(s.dist == "500 m" && s.street == "Lê Lợi" && s.eta == "12 min" && s.turn == .right)
    }

    @Test func headingUpRotates() {
        // Heading east: a route running east goes straight up the screen.
        let pts = stride(from: 0.0, through: 500, by: 10).map { north(0, east: $0) }
        let route = Route(points: pts, distance: 500, time: 300, instructions: [])
        let s = buildScene(progress: progress(heading: 90), route: route, streets: [], state: .navigating)
        #expect(s.route.allSatisfy { $0.x == 120 })
        #expect(s.route.last!.y < 100)
    }

    @Test func skipsPassedRoutePoints() {
        let route = straightRoute(length: 400)
        let p = progress(snapped: north(300), along: 300)
        let s = buildScene(progress: p, route: route, streets: [], state: .navigating)
        #expect(s.route.first == ScreenPoint(x: 120, y: 200))
        let mpp = metersPerPixel(distanceToTurn: 500, speed: 0)
        // A straight route simplifies to its two ends.
        #expect(s.route.last == ScreenPoint(x: 120, y: Int16((200 - 100 / mpp).rounded())))
        #expect(s.route.allSatisfy { $0.y <= 200 })
    }

    @Test func rejoinDirection() {
        var p = progress(heading: 90)
        p.offRoute = true; p.distanceToRoute = 83; p.bearingToRoute = 180
        let s = buildScene(progress: p, route: straightRoute(), streets: [], state: .rerouting)
        #expect(s.turn == .rejoin)
        #expect(s.dir == 64)   // 90 deg clockwise from screen-up
        #expect(s.dist == "80 m")
    }

    @Test func arrivedHasNoMap() {
        var p = progress(); p.arrived = true
        let s = buildScene(progress: p, route: straightRoute(), streets: [], state: .arrived)
        #expect(s.turn == .arrive && s.route.isEmpty && s.streets.isEmpty && s.dist == "" && s.eta == "")
    }

    /// 300 streets x 10 points crossing the whole area, plus a long zig-zag route.
    private func dense() -> (Route, [StreetLine]) {
        var streets: [StreetLine] = []
        for i in 0..<300 {
            let off = Double(i % 30) * 12 - 180
            let pts = (0..<10).map { k -> Coord in
                let a = Double(k) * 40 - 200
                let jig = (k % 2 == 0 ? 3.0 : -3.0) * 5
                return i % 2 == 0 ? north(a, east: off + jig) : north(off + jig, east: a)
            }
            streets.append(StreetLine(cls: UInt8(i % 4), points: pts))
        }
        // Route: ~200 points that really need to stay (zig-zag of 20 m legs over 250 px).
        let rp = (0..<200).map { k in north(Double(k) * 1.5, east: k % 2 == 0 ? 0 : 6) }
        let route = Route(points: rp, distance: 300, time: 200, instructions: [])
        return (route, streets)
    }

    @Test func denseAreaWithinAllLimits() {
        let (route, streets) = dense()
        let s = buildScene(progress: progress(toTurn: 100), route: route, streets: streets, state: .navigating)
        let d = encode(s)
        #expect(d.count <= 8192)
        #expect(s.route.count <= 256 && s.route.count >= 2)
        #expect(s.streets.count <= 96)
        #expect(s.streets.reduce(0) { $0 + $1.points.count } <= 640)
        #expect(s.streets.allSatisfy { $0.points.count >= 2 && $0.cls <= 3 })
        #expect(allPoints(s).allSatisfy { (-64...304).contains(Int($0.x)) && (-64...344).contains(Int($0.y)) })
        #expect(!s.streets.isEmpty)
    }

    @Test func denseKeepsEveryRoutePointWhenSmallBudget() {
        let (_, streets) = dense()
        // 60 zig-zag points: every one survives simplification and fits in 512 bytes with room for streets.
        let route = Route(points: (0..<60).map { north(Double($0) * 1.5, east: $0 % 2 == 0 ? 0 : 6) },
                          distance: 90, time: 60, instructions: [])
        let full = buildScene(progress: progress(toTurn: 100), route: route, streets: [], state: .navigating)
        #expect(full.route.count == 60)
        let small = buildScene(progress: progress(toTurn: 100), route: route, streets: streets, state: .navigating,
                               maxBytes: chunkCapacity(maxWrite: 20))
        #expect(encode(small).count <= 512)
        #expect(small.route == full.route)
        #expect(!small.streets.isEmpty)
    }

    @Test func denseFitsChunksAt185() {
        let (route, streets) = dense()
        let s = buildScene(progress: progress(toTurn: 100), route: route, streets: streets, state: .navigating,
                           maxBytes: chunkCapacity(maxWrite: 185))
        #expect(chunks(encode(s), seq: 1, maxWrite: 185).count <= 32)
    }

    @Test func dropsLowPriorityStreetsFirst() {
        let near = StreetLine(cls: 3, points: [north(0, east: 5), north(50, east: 5)])
        let major = StreetLine(cls: 0, points: [north(0, east: 150), north(50, east: 150)])
        // Budget fits the route and exactly one street.
        let route = straightRoute(length: 100)
        let base = buildScene(progress: progress(), route: route, streets: [], state: .navigating)
        let one = encode(base).count + 2 + 8
        let s = buildScene(progress: progress(), route: route, streets: [near, major], state: .navigating, maxBytes: one)
        #expect(s.streets.count == 1 && s.streets[0].cls == 0)
    }

    @Test func clipsToBox() {
        // A route running 5 km ahead leaves the clip box; it is cut at the edge.
        let route = straightRoute(length: 5000, step: 50)
        let s = buildScene(progress: progress(toTurn: 300), route: route, streets: [], state: .navigating)
        #expect(s.route.allSatisfy { $0.y >= -64 })
        #expect(s.route.last!.y == -64)
    }
}

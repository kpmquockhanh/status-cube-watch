import Testing
import Foundation
@testable import NavCore

private let origin = Coord(lat: 50, lon: 10)
private let mPerDegLat = 6_371_000.0 * .pi / 180
private func north(_ m: Double, east e: Double = 0, from c: Coord = origin) -> Coord {
    Coord(lat: c.lat + m / mPerDegLat, lon: c.lon + e / (mPerDegLat * cos(c.lat * .pi / 180)))
}

/// 500 m north, then 300 m east. Points every 100 m; corner is point 5.
private func lRoute() -> Route {
    var pts = (0...5).map { north(Double($0) * 100) }
    let corner = pts[5]
    for i in 1...3 { pts.append(north(0, east: Double(i) * 100, from: corner)) }
    return Route(points: pts, distance: 800, time: 160, instructions: [
        Instruction(turn: .straight, streetName: "A", distance: 500, time: 100, interval: 0...5),
        Instruction(turn: .right, streetName: "B", distance: 300, time: 60, interval: 5...8),
        Instruction(turn: .arrive, streetName: "", distance: 0, time: 0, interval: 8...8),
    ])
}

private func fix(_ c: Coord, acc: Double = 5, speed: Double = 10, course: Double = -1) -> Fix {
    Fix(coord: c, accuracy: acc, speed: speed, course: course)
}

@Test func distanceToTurnCountsDownMonotonically() {
    let t = RouteTracker(route: lRoute())
    var last = Double.infinity
    for m in stride(from: 10.0, through: 480, by: 35) {
        let p = t.update(fix(north(m)))
        #expect(p.nextInstruction == 1)
        #expect(p.distanceToTurn < last)
        #expect(abs(p.distanceToTurn - (500 - m)) < 1)
        #expect(!p.offRoute)
        last = p.distanceToTurn
    }
}

@Test func turnAdvancesAfterCorner() {
    let t = RouteTracker(route: lRoute())
    _ = t.update(fix(north(450)))
    let corner = north(500)
    let p = t.update(fix(north(0, east: 50, from: corner)))
    #expect(p.nextInstruction == 2)
    #expect(abs(p.distanceToTurn - 250) < 1)
    #expect(abs(p.distanceAlong - 550) < 1)
    #expect(abs(p.remainingTime - 160 * 250 / 800) < 0.5)
}

@Test func standingJitterKeepsHeadingAndStaysOnRoute() {
    let t = RouteTracker(route: lRoute())
    _ = t.update(fix(north(200), speed: 12, course: 3))
    let spot = north(200)
    let noise: [(Double, Double)] = [(8, 0), (-8, 3), (2, 8), (-5, -8), (7, 7), (-7, 5), (0, -8), (6, -6)]
    var headings = Set<Double>()
    for i in 0..<20 {
        let (n, e) = noise[i % noise.count]
        let p = t.update(fix(north(n, east: e, from: spot), acc: 10, speed: 0, course: Double((i * 97) % 360)))
        headings.insert(p.heading)
        #expect(!p.offRoute)
    }
    #expect(headings == [3])
}

@Test func headingFallsBackToRouteBearing() {
    let t = RouteTracker(route: lRoute())
    let p = t.update(fix(north(100), speed: -1, course: -1))
    #expect(abs(p.heading - 0) < 0.5 || abs(p.heading - 360) < 0.5)
}

@Test func offRouteNeedsThreeGoodFixes() {
    let t = RouteTracker(route: lRoute())
    let off = north(200, east: 60)
    #expect(!t.update(fix(off, acc: 10)).offRoute)
    #expect(!t.update(fix(off, acc: 10)).offRoute)
    let p = t.update(fix(off, acc: 10))
    #expect(p.offRoute)
    #expect(abs(p.distanceToRoute - 60) < 1)
    #expect(abs(p.bearingToRoute - 270) < 1)
    // back on the route clears it
    #expect(!t.update(fix(north(210), acc: 10)).offRoute)
}

@Test func weakFixDoesNotCountTowardOffRoute() {
    let t = RouteTracker(route: lRoute())
    let off = north(200, east: 60)
    _ = t.update(fix(off, acc: 10))
    _ = t.update(fix(off, acc: 10))
    let weak = t.update(fix(off, acc: 50))
    #expect(weak.gpsWeak)
    #expect(!weak.offRoute)
    #expect(!t.update(fix(off, acc: 50)).offRoute)
    #expect(t.update(fix(off, acc: 10)).offRoute)
}

@Test func arrivesWithin30mOfEnd() {
    let t = RouteTracker(route: lRoute())
    let end = lRoute().points[8]
    #expect(!t.update(fix(north(0, east: -100, from: end))).arrived)
    let p = t.update(fix(north(0, east: -20, from: end)))
    #expect(p.arrived)
    #expect(!p.gpsWeak)
}

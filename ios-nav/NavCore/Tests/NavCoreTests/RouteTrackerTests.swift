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
    #expect(!t.update(fix(north(0, east: -200, from: end))).arrived)
    #expect(!t.update(fix(north(0, east: -110, from: end))).arrived)
    #expect(!t.update(fix(north(0, east: -50, from: end))).arrived)
    let p = t.update(fix(north(0, east: -20, from: end)))
    #expect(p.arrived)
    #expect(!p.gpsWeak)
}

private func pathRoute(_ pts: [Coord]) -> Route {
    Route(points: pts, distance: 0, time: 100, instructions: [
        Instruction(turn: .straight, streetName: "", distance: 0, time: 0, interval: 0...0),
        Instruction(turn: .arrive, streetName: "", distance: 0, time: 0, interval: (pts.count - 1)...(pts.count - 1)),
    ])
}

@Test func loopRouteDoesNotArriveAtStart() {
    // 400 m north, 200 m east, 400 m south, ending 10 m short of the start.
    var pts = (0...4).map { north(Double($0) * 100) }
    pts.append(north(400, east: 200))
    pts.append(north(0, east: 200))
    pts.append(north(0, east: 10))
    let t = RouteTracker(route: pathRoute(pts))
    for m in [0.0, 5, 10] {
        let p = t.update(fix(north(m), speed: 5))
        #expect(!p.arrived)
    }
    var last = t.update(fix(north(300)))
    #expect(!last.arrived)
    for m in [400.0, 500, 600] {   // corner then the east leg, as ridden
        last = t.update(fix(north(400, east: m - 400), speed: 8))
        #expect(!last.arrived)
    }
    for m in [350.0, 250, 150, 50] {
        last = t.update(fix(north(m, east: 200), speed: 8))
        #expect(!last.arrived)
    }
    last = t.update(fix(north(0, east: 15)))
    #expect(last.arrived)
}

@Test func weakFixNearEndDoesNotLatchArrival() {
    let t = RouteTracker(route: lRoute())
    let end = lRoute().points[8]
    _ = t.update(fix(north(0, east: -250, from: end)))
    #expect(!t.update(fix(north(0, east: -10, from: end), acc: 50)).arrived)
    #expect(!t.update(fix(north(0, east: -100, from: end))).arrived)
}

@Test func outAndBackStaysOnOutboundLeg() {
    // 500 m north, then 500 m back south on a line 8 m east.
    var pts = (0...5).map { north(Double($0) * 100) }
    pts += (0...5).map { north(Double(5 - $0) * 100, east: 8) }
    let route = Route(points: pts, distance: 1000, time: 100, instructions: [
        Instruction(turn: .straight, streetName: "", distance: 500, time: 0, interval: 0...5),
        Instruction(turn: .uTurn, streetName: "", distance: 500, time: 0, interval: 5...10),
        Instruction(turn: .arrive, streetName: "", distance: 0, time: 0, interval: 11...11),
    ])
    let t = RouteTracker(route: route)
    let jitter: [Double] = [-6, 5, 6, -4, 3, -6, 6, 2]
    var lastNext = 0, lastAlong = -1.0
    for (i, m) in stride(from: 5.0, through: 480, by: 15).enumerated() {
        let p = t.update(fix(north(m, east: jitter[i % jitter.count]), acc: 8))
        #expect(p.distanceAlong < 520)
        #expect(p.nextInstruction >= lastNext)
        #expect(p.distanceAlong > lastAlong - 40)
        lastNext = p.nextInstruction; lastAlong = p.distanceAlong
    }
}

@Test func singleFarFixDoesNotJumpToLaterLeg() {
    // U-shaped: 500 m north, 70 m east, 500 m south. A fix 60 m east of the first leg is
    // 10 m from the return leg (later in the route) but must not snap there.
    var pts = (0...5).map { north(Double($0) * 100) }
    pts += (0...5).map { north(Double(5 - $0) * 100, east: 70) }
    let t = RouteTracker(route: pathRoute(pts))
    _ = t.update(fix(north(240)))
    let p = t.update(fix(north(250, east: 60)))
    #expect(p.distanceAlong < 300)
    #expect(!p.offRoute)
}

@Test func roundTripFirstFixSnapsToStartAndDoesNotArrive() {
    // Out 400 m north, 100 m east, 400 m back south; the end is 3 m from the start.
    var pts = (0...4).map { north(Double($0) * 100) }
    pts.append(north(400, east: 100))
    pts.append(north(0, east: 100))
    pts.append(north(0, east: 3))
    let t = RouteTracker(route: pathRoute(pts))
    let p = t.update(fix(north(0, east: 6), speed: 0))
    #expect(!p.arrived)
    #expect(p.distanceAlong < 10)
    #expect(!t.update(fix(north(20), speed: 5, course: 0)).arrived)
    for m in stride(from: 100.0, through: 400, by: 50) { #expect(!t.update(fix(north(m), course: 0)).arrived) }
    _ = t.update(fix(north(400, east: 100), course: 90))
    #expect(!t.update(fix(north(200, east: 100), course: 180)).arrived)
    _ = t.update(fix(north(100, east: 100), course: 180))
    _ = t.update(fix(north(0, east: 100), course: 270))
    #expect(t.update(fix(north(0, east: 10), course: 270)).arrived)
}

@Test func outAndBackFirstFixUsesCourse() {
    var pts = (0...5).map { north(Double($0) * 100) }
    pts += (0...5).map { north(Double(5 - $0) * 100, east: 8) }
    let route = pathRoute(pts)
    let up = RouteTracker(route: route).update(fix(north(250, east: 4), course: 0))
    #expect(up.distanceAlong < 300)
    let down = RouteTracker(route: route).update(fix(north(250, east: 4), course: 180))
    #expect(down.distanceAlong > 700)
}

@Test func recoversAfterGpsGapWithoutOffRouteFlash() {
    // U: 500 m north, 60 m east, 500 m south. Rider is at 50 m, then reappears on the return leg.
    var pts = (0...5).map { north(Double($0) * 100) }
    pts += (0...5).map { north(Double(5 - $0) * 100, east: 60) }
    let t = RouteTracker(route: pathRoute(pts))
    _ = t.update(fix(north(50)))
    let there = north(450, east: 60)
    var last = t.update(fix(there, acc: 50))
    #expect(!last.offRoute)
    #expect(last.distanceAlong < 200)
    last = t.update(fix(there, acc: 50)); #expect(!last.offRoute)
    last = t.update(fix(there, acc: 50)); #expect(!last.offRoute)
    #expect(last.distanceAlong > 600)
    last = t.update(fix(there, acc: 10))
    #expect(!last.offRoute)
    #expect(last.distanceAlong > 600)
}

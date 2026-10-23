import Foundation
import Testing
@testable import NavCore

private let origin = Coord(lat: 21.0, lon: 105.8)
private let mPerDegLat = 6_371_000.0 * .pi / 180

/// 2 km due north, `east` meters east of `origin`, taking `time` seconds.
private func line(east: Double = 0, time: Double, distance: Double = 2000) -> Route {
    let dLon = east / (mPerDegLat * cos(origin.lat * .pi / 180))
    let pts = (0...20).map { Coord(lat: origin.lat + Double($0) * 100 / mPerDegLat, lon: origin.lon + dLon) }
    return Route(points: pts, distance: distance, time: time, instructions: [])
}

@Suite struct RouteOptionsTests {
    @Test func orderAndTitles() {
        let o = routeOptions([.fewestTurns: line(east: 400, time: 620), .fastest: line(east: 200, time: 600),
                              .calm: line(time: 650)])
        #expect(o.map(\.title) == ["Calm", "Fastest", "Fewest turns"])
        #expect(o.map(\.style) == [.calm, .fastest, .fewestTurns])
    }

    @Test func detourHidden() {
        // calm: +50% but exactly +300 s → shown; turns: +67% and +400 s → hidden
        let o = routeOptions([.fastest: line(time: 600), .calm: line(east: 200, time: 900),
                              .fewestTurns: line(east: 400, time: 1000)])
        #expect(o.map(\.style) == [.calm, .fastest])
    }

    @Test func shortTripNotCut() {
        let o = routeOptions([.fastest: line(time: 300), .calm: line(east: 200, time: 420)])
        #expect(o.map(\.style) == [.calm, .fastest])
    }

    @Test func longTripSmallShareNotCut() {
        // +400 s but only +11%
        let o = routeOptions([.fastest: line(time: 3600), .fewestTurns: line(east: 200, time: 4000)])
        #expect(o.map(\.style) == [.fastest, .fewestTurns])
    }

    @Test func noFastestNoFilter() {
        let o = routeOptions([.calm: line(time: 3000), .fewestTurns: line(east: 200, time: 4000)])
        #expect(o.map(\.style) == [.calm, .fewestTurns])
    }

    @Test func missingStylesStillListed() {
        let o = routeOptions([.calm: line(time: 600)])
        #expect(o.count == 1)
        #expect(o.first?.style == .calm)
        #expect(routeOptions([:]).isEmpty)
    }

    @Test func identicalRoutesMerge() {
        let r = line(time: 600)
        let o = routeOptions([.calm: r, .fastest: r, .fewestTurns: r])
        #expect(o.count == 1)
        #expect(o[0].title == "Calm · Fastest · Fewest turns")
        #expect(o[0].style == .calm)
        #expect(o[0].styles == [.calm, .fastest, .fewestTurns])
    }

    @Test func partialMerge() {
        let o = routeOptions([.calm: line(time: 650), .fastest: line(east: 200, time: 600),
                              .fewestTurns: line(east: 210, time: 610)])
        #expect(o.map(\.title) == ["Calm", "Fastest · Fewest turns"])
    }

    @Test func sameRouteTolerances() {
        let a = line(time: 600)
        #expect(sameRoute(a, line(east: 20, time: 650, distance: 2030)))
        #expect(!sameRoute(a, line(east: 40, time: 600)))
        #expect(!sameRoute(a, line(time: 600, distance: 2050)))
        // b follows a and then goes on 300 m: every point of b is not near a
        var b = a; b.points.append(Coord(lat: a.points.last!.lat + 300 / mPerDegLat, lon: a.points.last!.lon))
        #expect(!sameRoute(a, b))
        #expect(!sameRoute(b, a))
    }

    @Test func defaultPickRules() {
        let all = routeOptions([.calm: line(time: 650), .fastest: line(east: 200, time: 600),
                                .fewestTurns: line(east: 400, time: 620)])
        #expect(defaultPick(all, last: .fewestTurns) == 2)
        #expect(defaultPick(all, last: nil) == 0)
        let noCalm = routeOptions([.fastest: line(time: 600), .fewestTurns: line(east: 400, time: 620)])
        #expect(defaultPick(noCalm, last: nil) == 0)
        #expect(defaultPick(noCalm, last: .calm) == 0)
        let merged = routeOptions([.calm: line(east: 200, time: 650), .fastest: line(time: 600),
                                   .fewestTurns: line(time: 600)])
        #expect(defaultPick(merged, last: .fewestTurns) == 1)
        #expect(defaultPick([], last: .calm) == nil)
    }
}

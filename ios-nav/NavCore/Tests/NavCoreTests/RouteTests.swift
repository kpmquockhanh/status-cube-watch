import Foundation
import Testing
@testable import NavCore

func fixtureData(_ name: String) throws -> Data {
    let url = try #require(Bundle.module.url(forResource: name, withExtension: "json", subdirectory: "Fixtures"))
    return try Data(contentsOf: url)
}

@Suite struct RouteTests {
    @Test func decodesFixture() throws {
        let data = try fixtureData("route-hcmc")
        let route = try RouteDecoding.decode(data)
        let json = try #require(JSONSerialization.jsonObject(with: data) as? [String: Any])
        let path = try #require((json["paths"] as? [[String: Any]])?.first)
        let coords = try #require((path["points"] as? [String: Any])?["coordinates"] as? [[Double]])
        #expect(route.instructions.count > 1)
        #expect(route.instructions.last?.turn == .arrive)
        #expect(route.points.count == coords.count)
        #expect(route.distance == (path["distance"] as? Double))
        #expect(route.points[0] == Coord(lat: coords[0][1], lon: coords[0][0]))
    }

    @Test func signTable() {
        for s in [-98, -8, 8] { #expect(navTurn(graphHopperSign: s) == .uTurn) }
        #expect(navTurn(graphHopperSign: -7) == .keepLeft)
        #expect(navTurn(graphHopperSign: 7) == .keepRight)
        #expect(navTurn(graphHopperSign: -3) == .sharpLeft)
        #expect(navTurn(graphHopperSign: -2) == .left)
        #expect(navTurn(graphHopperSign: -1) == .slightLeft)
        #expect(navTurn(graphHopperSign: 0) == .straight)
        #expect(navTurn(graphHopperSign: 1) == .slightRight)
        #expect(navTurn(graphHopperSign: 2) == .right)
        #expect(navTurn(graphHopperSign: 3) == .sharpRight)
        #expect(navTurn(graphHopperSign: 4) == .arrive)
        #expect(navTurn(graphHopperSign: 5) == .arrive)
        #expect(navTurn(graphHopperSign: 6) == .roundabout)
        #expect(navTurn(graphHopperSign: -6) == .roundabout)
        #expect(navTurn(graphHopperSign: 42) == .straight)
    }

    @Test func rawValuesMatchCube() {
        #expect(NavTurn.rejoin.rawValue == 12)
        #expect(NavTurn.arrive.rawValue == 11)
        #expect(NavTurn.roundabout.rawValue == 10)
        #expect(NavTurn.uTurn.rawValue == 7)
        #expect(NavTurn.sharpRight.rawValue == 6)
    }

    @Test func roundaboutExit() throws {
        let json = #"{"paths":[{"distance":10,"time":1000,"points":{"coordinates":[[106.0,10.0],[106.1,10.1]]},"instructions":[{"sign":6,"exit_number":3,"street_name":"X","distance":10,"time":1000,"interval":[0,1]}]}]}"#
        let r = try RouteDecoding.decode(Data(json.utf8))
        #expect(r.instructions[0].turn == .roundabout)
        #expect(r.instructions[0].exit == 3)
    }

    @Test func geo() {
        let a = Coord(lat: 10.0, lon: 106.0)
        let north = Coord(lat: 10.01, lon: 106.0)
        let east = Coord(lat: 10.0, lon: 106.01)
        #expect(abs(distanceMeters(a, north) - 1111.9) < 2)
        #expect(abs(bearingDegrees(a, north) - 0) < 0.01)
        #expect(abs(bearingDegrees(a, east) - 90) < 0.1)
        #expect(abs(bearingDegrees(north, a) - 180) < 0.01)
    }

    @Test func badJSONThrows() {
        #expect(throws: (any Error).self) { try RouteDecoding.decode(Data("{}".utf8)) }
    }
}

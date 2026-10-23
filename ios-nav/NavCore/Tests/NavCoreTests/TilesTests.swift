import Foundation
import Testing
@testable import NavCore

@Suite struct TilesTests {
    @Test func tileForCoord() {
        #expect(tile(for: Coord(lat: 10.7725, lon: 106.6980)) == TileID(x: 26095, y: 15397))
    }

    @Test func corridor() {
        let a = Coord(lat: 10.7725, lon: 106.6980)
        // ~1 km east: 1 km / (111320 * cos(lat)) degrees of longitude
        let dLon = 1000 / (111_320 * cos(a.lat * .pi / 180))
        let b = Coord(lat: a.lat, lon: a.lon + dLon)
        let dLat250 = 250 / 111_320.0
        let dLat2k = 2000 / 111_320.0
        let mid = Coord(lat: a.lat + dLat250, lon: a.lon + dLon / 2)
        let far = Coord(lat: a.lat + dLat2k, lon: a.lon + dLon / 2)
        let tiles = corridorTiles(route: [a, b])
        #expect(tiles.contains(tile(for: a)))
        #expect(tiles.contains(tile(for: b)))
        #expect(tiles.contains(tile(for: mid)))
        #expect(!tiles.contains(tile(for: far)))
    }

    @Test func corridorSinglePoint() {
        let a = Coord(lat: 10.7725, lon: 106.6980)
        #expect(corridorTiles(route: [a]).contains(tile(for: a)))
        #expect(corridorTiles(route: []).isEmpty)
    }

    @Test func decodesFixture() throws {
        let lines = try TileDecoding.decode(try fixtureData("tile-26095-15397"))
        #expect(!lines.isEmpty)
        for l in lines {
            #expect(l.cls <= 3)
            #expect(l.points.count >= 2)
            for p in l.points {
                #expect(abs(p.lat - 10.77) < 0.1)
                #expect(abs(p.lon - 106.69) < 0.1)
            }
        }
    }

    @Test func decodesSmall() throws {
        let j = #"{"v":1,"z":15,"x":1,"y":2,"w":[[1,10773144,106691477,10773263,106691541]]}"#
        let l = try TileDecoding.decode(Data(j.utf8))
        #expect(l == [StreetLine(cls: 1, points: [Coord(lat: 10.773144, lon: 106.691477), Coord(lat: 10.773263, lon: 106.691541)])])
    }

    @Test func badTileThrows() {
        #expect(throws: (any Error).self) { try TileDecoding.decode(Data("nope".utf8)) }
    }
}

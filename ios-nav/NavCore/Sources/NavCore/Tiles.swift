import Foundation

private let zoom = 15
private let tileCount = Double(1 << 15)

public struct TileID: Hashable, Codable, Sendable {
    public var x: Int
    public var y: Int
    public init(x: Int, y: Int) { self.x = x; self.y = y }
}

private func tileXY(lat: Double, lon: Double) -> TileID {
    let latC = max(-85.0511, min(85.0511, lat))
    let fx = (lon + 180) / 360 * tileCount
    let r = latC * .pi / 180
    let fy = (1 - log(tan(r) + 1 / cos(r)) / .pi) / 2 * tileCount
    let maxI = Int(tileCount) - 1
    return TileID(x: max(0, min(maxI, Int(fx.rounded(.down)))), y: max(0, min(maxI, Int(fy.rounded(.down)))))
}

public func tile(for c: Coord) -> TileID { tileXY(lat: c.lat, lon: c.lon) }

/// Tiles within `bufferMeters` of the route, sampled every 50 m.
public func corridorTiles(route: [Coord], bufferMeters: Double = 300) -> Set<TileID> {
    var out = Set<TileID>()
    guard let first = route.first else { return out }

    func addBox(_ c: Coord) {
        let dLat = bufferMeters / 111_320
        let dLon = bufferMeters / (111_320 * max(0.01, cos(c.lat * .pi / 180)))
        let a = tileXY(lat: c.lat + dLat, lon: c.lon - dLon)   // top-left (min x, min y)
        let b = tileXY(lat: c.lat - dLat, lon: c.lon + dLon)   // bottom-right
        for x in a.x...b.x { for y in a.y...b.y { out.insert(TileID(x: x, y: y)) } }
    }

    addBox(first)
    let step = 50.0
    for i in 1..<max(1, route.count) {
        let p = route[i - 1], q = route[i]
        let d = distanceMeters(p, q)
        let n = max(1, Int((d / step).rounded(.up)))
        for k in 1...n {
            let t = Double(k) / Double(n)
            addBox(Coord(lat: p.lat + (q.lat - p.lat) * t, lon: p.lon + (q.lon - p.lon) * t))
        }
    }
    return out
}

public struct StreetLine: Equatable, Sendable {
    public var cls: UInt8
    public var points: [Coord]
    public init(cls: UInt8, points: [Coord]) { self.cls = cls; self.points = points }
}

public enum TileDecoding {
    public struct Failure: Error, Equatable { public var reason: String }
    private struct Body: Decodable { var w: [[Int]] }

    /// `{"v":1,"z":15,"x":X,"y":Y,"w":[[cls, lat0, lon0, ...], ...]}`, degrees x 1e6, latitude first.
    public static func decode(_ data: Data) throws -> [StreetLine] {
        let body = try JSONDecoder().decode(Body.self, from: data)
        var out: [StreetLine] = []
        out.reserveCapacity(body.w.count)
        for way in body.w {
            guard way.count >= 5, way.count % 2 == 1 else { throw Failure(reason: "bad way") }
            var pts: [Coord] = []
            pts.reserveCapacity((way.count - 1) / 2)
            var i = 1
            while i + 1 < way.count {
                pts.append(Coord(lat: Double(way[i]) / 1e6, lon: Double(way[i + 1]) / 1e6))
                i += 2
            }
            out.append(StreetLine(cls: UInt8(clamping: way[0]), points: pts))
        }
        return out
    }
}

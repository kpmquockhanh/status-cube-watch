import Foundation

public struct Coord: Equatable, Codable, Sendable {
    public var lat: Double
    public var lon: Double
    public init(lat: Double, lon: Double) { self.lat = lat; self.lon = lon }
}

private let earthRadius = 6_371_000.0
private func rad(_ d: Double) -> Double { d * .pi / 180 }

/// Haversine distance in meters.
public func distanceMeters(_ a: Coord, _ b: Coord) -> Double {
    let dLat = rad(b.lat - a.lat), dLon = rad(b.lon - a.lon)
    let h = sin(dLat / 2) * sin(dLat / 2) + cos(rad(a.lat)) * cos(rad(b.lat)) * sin(dLon / 2) * sin(dLon / 2)
    return 2 * earthRadius * asin(min(1, sqrt(h)))
}

/// Initial bearing, 0 = north, clockwise, in [0, 360).
public func bearingDegrees(_ a: Coord, _ b: Coord) -> Double {
    let dLon = rad(b.lon - a.lon)
    let y = sin(dLon) * cos(rad(b.lat))
    let x = cos(rad(a.lat)) * sin(rad(b.lat)) - sin(rad(a.lat)) * cos(rad(b.lat)) * cos(dLon)
    let deg = atan2(y, x) * 180 / .pi
    return (deg + 360).truncatingRemainder(dividingBy: 360)
}

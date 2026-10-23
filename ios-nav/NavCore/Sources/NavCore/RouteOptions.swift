import Foundation

/// One row of the preview's route list: a route and every style that produced it.
public struct RouteOption: Equatable, Sendable {
    /// In display order; the first is the style the ride keeps.
    public var styles: [RouteStyle]
    public var route: Route
    public var style: RouteStyle { styles[0] }
    public var title: String { styles.map(\.title).joined(separator: " · ") }
}

/// A Calm or Fewest turns route is hidden when it is both 20% and 5 minutes slower than Fastest.
let detourShare = 1.2
let detourSeconds: Double = 300

/// Rows in display order. The detour limit applies only when Fastest is there; a route that is
/// the same as an earlier row's joins that row.
public func routeOptions(_ routes: [RouteStyle: Route]) -> [RouteOption] {
    let fastest = routes[.fastest]
    var out: [RouteOption] = []
    for style in RouteStyle.allCases {
        guard let r = routes[style] else { continue }
        if let f = fastest, style != .fastest,
           r.time > detourShare * f.time, r.time - f.time > detourSeconds { continue }
        if let i = out.firstIndex(where: { sameRoute($0.route, r) }) {
            out[i].styles.append(style)
        } else {
            out.append(RouteOption(styles: [style], route: r))
        }
    }
    return out
}

/// Distances within 2% and every point of each within 25 m of the other's line.
func sameRoute(_ a: Route, _ b: Route) -> Bool {
    guard max(a.distance, b.distance) <= 1.02 * min(a.distance, b.distance) else { return false }
    return a.points.allSatisfy { distanceToLine($0, b.points) <= 25 }
        && b.points.allSatisfy { distanceToLine($0, a.points) <= 25 }
}

/// Meters from `p` to the polyline, straight segments in a local flat frame around `p`.
private func distanceToLine(_ p: Coord, _ line: [Coord]) -> Double {
    guard let first = line.first else { return .infinity }
    let kx = 111_194.9266 * cos(p.lat * .pi / 180), ky = 111_194.9266
    func xy(_ c: Coord) -> (Double, Double) { ((c.lon - p.lon) * kx, (c.lat - p.lat) * ky) }
    var (ax, ay) = xy(first)
    var best = (ax * ax + ay * ay).squareRoot()
    for c in line.dropFirst() {
        let (bx, by) = xy(c)
        let dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy
        let t = len2 > 0 ? max(0, min(1, -(ax * dx + ay * dy) / len2)) : 0
        let x = ax + t * dx, y = ay + t * dy
        best = min(best, (x * x + y * y).squareRoot())
        (ax, ay) = (bx, by)
    }
    return best
}

/// The row with the rider's last style, else the one with Calm, else the first; nil when empty.
public func defaultPick(_ options: [RouteOption], last: RouteStyle?) -> Int? {
    guard !options.isEmpty else { return nil }
    if let last, let i = options.firstIndex(where: { $0.styles.contains(last) }) { return i }
    return options.firstIndex { $0.styles.contains(.calm) } ?? 0
}

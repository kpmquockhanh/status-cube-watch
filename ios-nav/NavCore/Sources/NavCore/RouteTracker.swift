import Foundation

/// One GPS fix, as the phone's location service reports it.
public struct Fix: Sendable {
    public var coord: Coord
    /// Horizontal accuracy, meters.
    public var accuracy: Double
    /// m/s, negative = unknown.
    public var speed: Double
    /// Degrees from north, negative = unknown.
    public var course: Double
    public init(coord: Coord, accuracy: Double, speed: Double, course: Double) {
        self.coord = coord; self.accuracy = accuracy; self.speed = speed; self.course = course
    }
}

public struct Progress: Equatable, Sendable {
    public var snapped: Coord
    /// Degrees, 0 = north. GPS course above 3 km/h, otherwise the last heading.
    public var heading: Double
    /// Meters along the route from its start to the snapped position.
    public var distanceAlong: Double
    /// Index into `Route.instructions` of the next maneuver (the last one when none is ahead).
    public var nextInstruction: Int
    /// Meters along the route from the snapped position to the START of the next
    /// instruction's interval (the maneuver point). With no instructions: to the route end.
    public var distanceToTurn: Double
    /// Seconds: the route's time scaled by the remaining distance.
    public var remainingTime: Double
    public var offRoute: Bool
    /// Meters from the fix to the snapped position.
    public var distanceToRoute: Double
    /// Bearing from the fix toward the snapped position (rejoin arrow).
    public var bearingToRoute: Double
    public var arrived: Bool
    public var gpsWeak: Bool
}

/// Follows a route on successive GPS fixes. Offline; straight segments in a local flat
/// projection around each fix, which is accurate enough at city scale.
public final class RouteTracker {
    static let minHeadingSpeed = 3.0 / 3.6   // m/s
    static let offRouteDistance = 40.0
    static let offRouteFixes = 3
    static let goodAccuracy = 30.0
    static let arriveDistance = 30.0
    static let backWindow = 100.0
    static let forwardWindow = 2000.0
    static let backFree = 30.0
    static let forwardFree = 100.0
    static let arriveRemaining = 60.0

    private let route: Route
    private let cum: [Double]      // cum[i] = meters along the route at points[i]
    private var segment = 0
    private var lastAlong = 0.0
    private var lastHeading: Double?
    private var badFixes = 0
    private var arrivedLatch = false
    private var started = false

    public init(route: Route) {
        self.route = route
        var c = [0.0]
        for i in 1..<max(route.points.count, 1) {
            c.append(c[i - 1] + distanceMeters(route.points[i - 1], route.points[i]))
        }
        cum = c
    }

    public func update(_ fix: Fix) -> Progress {
        let pts = route.points
        guard pts.count >= 2 else {
            return Progress(snapped: pts.first ?? fix.coord, heading: fix.course >= 0 ? fix.course : 0,
                            distanceAlong: 0, nextInstruction: 0, distanceToTurn: 0, remainingTime: 0,
                            offRoute: false, distanceToRoute: 0, bearingToRoute: 0,
                            arrived: true, gpsWeak: fix.accuracy > Self.goodAccuracy)
        }
        let lastSeg = pts.count - 2
        let kx = 111_194.9266 * cos(fix.coord.lat * .pi / 180)   // meters per degree lon
        let ky = 111_194.9266
        func xy(_ c: Coord) -> (Double, Double) { ((c.lon - fix.coord.lon) * kx, (c.lat - fix.coord.lat) * ky) }

        // Score = distance to the segment, plus (when `continuity`) a penalty for jumping along
        // the route relative to the last position: backward beyond 30 m costs 5 per meter, forward
        // beyond 100 m costs 1 per meter. Keeps out-and-back and looping routes on the right leg.
        func nearest(_ range: ClosedRange<Int>, continuity: Bool) -> (seg: Int, t: Double, d: Double, score: Double) {
            var best = (seg: range.lowerBound, t: 0.0, d: Double.infinity, score: Double.infinity)
            for i in range {
                let (ax, ay) = xy(pts[i]), (bx, by) = xy(pts[i + 1])
                let dx = bx - ax, dy = by - ay
                let len2 = dx * dx + dy * dy
                // The fix is the projection origin, so its position is (0, 0).
                let t = len2 > 0 ? min(1, max(0, -(ax * dx + ay * dy) / len2)) : 0
                let px = ax + t * dx, py = ay + t * dy
                let d = (px * px + py * py).squareRoot()
                var score = d
                if continuity {
                    let delta = cum[i] + t * (cum[i + 1] - cum[i]) - lastAlong
                    if delta < -Self.backFree { score += (-delta - Self.backFree) * 5 }
                    else if delta > Self.forwardFree { score += delta - Self.forwardFree }
                }
                if score < best.score { best = (i, t, d, score) }   // strict: ties keep the earlier segment
            }
            return best
        }

        // Window: up to 100 m back, up to 2 km ahead of the last position.
        var lo = segment
        while lo > 0 && lastAlong - cum[lo] < Self.backWindow { lo -= 1 }
        var hi = segment
        while hi < lastSeg && cum[hi + 1] - lastAlong < Self.forwardWindow { hi += 1 }
        // The first fix has no history: search the whole route by plain distance.
        var best = started ? nearest(lo...hi, continuity: true) : nearest(0...lastSeg, continuity: false)
        started = true

        let weak = fix.accuracy > Self.goodAccuracy
        if !weak {
            badFixes = best.d > Self.offRouteDistance ? badFixes + 1 : 0
        }
        // Only once off-route has tripped may the snap leave the continuity window.
        if badFixes >= Self.offRouteFixes {
            let all = nearest(0...lastSeg, continuity: false)
            if all.d < best.d {
                best = all
                if !weak && best.d <= Self.offRouteDistance { badFixes = 0 }
            }
        }

        let a = pts[best.seg], b = pts[best.seg + 1]
        let snapped = Coord(lat: a.lat + (b.lat - a.lat) * best.t, lon: a.lon + (b.lon - a.lon) * best.t)
        let segLen = cum[best.seg + 1] - cum[best.seg]
        let along = cum[best.seg] + best.t * segLen
        segment = best.seg
        lastAlong = along

        let total = cum[lastSeg + 1]
        let remaining = max(0, total - along)

        let next = route.instructions.firstIndex { $0.interval.lowerBound > best.seg }
            ?? max(0, route.instructions.count - 1)
        let toTurn: Double
        if route.instructions.isEmpty {
            toTurn = remaining
        } else {
            toTurn = max(0, cum[min(route.instructions[next].interval.lowerBound, lastSeg + 1)] - along)
        }

        let heading: Double
        if fix.speed >= Self.minHeadingSpeed && fix.course >= 0 {
            heading = fix.course
        } else if let h = lastHeading {
            heading = h
        } else {
            heading = bearingDegrees(a, b)
        }
        lastHeading = heading

        if !weak, remaining <= Self.arriveRemaining,
           distanceMeters(fix.coord, pts[lastSeg + 1]) <= Self.arriveDistance { arrivedLatch = true }

        return Progress(
            snapped: snapped, heading: heading, distanceAlong: along,
            nextInstruction: next, distanceToTurn: toTurn,
            remainingTime: total > 0 ? route.time * remaining / total : 0,
            offRoute: badFixes >= Self.offRouteFixes, distanceToRoute: best.d,
            bearingToRoute: bearingDegrees(fix.coord, snapped),
            arrived: arrivedLatch, gpsWeak: weak)
    }
}

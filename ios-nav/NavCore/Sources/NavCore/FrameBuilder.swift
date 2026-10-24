import Foundation

private let riderX = 120.0, riderY = 200.0
private let metersPerDegree = 111_194.9266

public func metersPerPixel(distanceToTurn: Double, speed: Double) -> Double {
    if distanceToTurn <= 200 { return 0.8 }
    if distanceToTurn > 2000 && speed > 11 { return 3.0 }
    return 1.5
}

private struct P { var x: Double; var y: Double }

/// Liang-Barsky against the cube's clip box; returns the visible parameter range.
private func clipSegment(_ a: P, _ b: P) -> (Double, Double)? {
    var t0 = 0.0, t1 = 1.0
    let dx = b.x - a.x, dy = b.y - a.y
    let checks: [(Double, Double)] = [
        (-dx, a.x - Double(NavLimits.minX)), (dx, Double(NavLimits.maxX) - a.x),
        (-dy, a.y - Double(NavLimits.minY)), (dy, Double(NavLimits.maxY) - a.y)]
    for (p, q) in checks {
        if p == 0 { if q < 0 { return nil } } else {
            let r = q / p
            if p < 0 { if r > t1 { return nil }; t0 = max(t0, r) } else { if r < t0 { return nil }; t1 = min(t1, r) }
        }
    }
    return (t0, t1)
}

/// Splits a polyline into the runs that lie inside the clip box.
private func clip(_ pts: [P]) -> [[P]] {
    var runs: [[P]] = []
    var cur: [P] = []
    func flush() { if cur.count >= 2 { runs.append(cur) }; cur = [] }
    guard pts.count >= 2 else { return [] }
    for i in 1..<pts.count {
        let a = pts[i - 1], b = pts[i]
        guard let (t0, t1) = clipSegment(a, b) else { flush(); continue }
        let s = P(x: a.x + (b.x - a.x) * t0, y: a.y + (b.y - a.y) * t0)
        let e = P(x: a.x + (b.x - a.x) * t1, y: a.y + (b.y - a.y) * t1)
        if cur.isEmpty || t0 > 0 { flush(); cur = [s] }
        cur.append(e)
        if t1 < 1 { flush() }
    }
    flush()
    return runs
}

private func douglasPeucker(_ pts: [P], epsilon: Double) -> [P] {
    guard pts.count > 2 else { return pts }
    var keep = [Bool](repeating: false, count: pts.count)
    keep[0] = true; keep[pts.count - 1] = true
    var stack = [(0, pts.count - 1)]
    while let (lo, hi) = stack.popLast() {
        guard hi > lo + 1 else { continue }
        let a = pts[lo], b = pts[hi]
        let dx = b.x - a.x, dy = b.y - a.y
        let len = (dx * dx + dy * dy).squareRoot()
        var worst = 0.0, idx = -1
        for i in (lo + 1)..<hi {
            let d = len > 0 ? abs(dy * (pts[i].x - a.x) - dx * (pts[i].y - a.y)) / len
                            : ((pts[i].x - a.x) * (pts[i].x - a.x) + (pts[i].y - a.y) * (pts[i].y - a.y)).squareRoot()
            if d > worst { worst = d; idx = i }
        }
        if worst > epsilon, idx > 0 { keep[idx] = true; stack.append((lo, idx)); stack.append((idx, hi)) }
    }
    return zip(pts, keep).filter { $0.1 }.map { $0.0 }
}

private func runDist(_ run: [P]) -> Double {
    run.reduce(Double.infinity) { min($0, hypot($1.x - riderX, $1.y - riderY)) }
}

private func round16(_ pts: [P]) -> [ScreenPoint] {
    var out: [ScreenPoint] = []
    for p in pts {
        let q = ScreenPoint(x: Int16(max(Double(NavLimits.minX), min(Double(NavLimits.maxX), p.x.rounded()))),
                            y: Int16(max(Double(NavLimits.minY), min(Double(NavLimits.maxY), p.y.rounded()))))
        if out.last != q { out.append(q) }
    }
    return out
}

private func frameBytes(dist: String, street: String, eta: String, route: Int, streets: [Int]) -> Int {
    1 + 5 + 3 + wireString(dist, limit: NavLimits.maxDist).count + wireString(street, limit: NavLimits.maxStreet).count
        + wireString(eta, limit: NavLimits.maxETA).count + 2 + 4 * route + 1 + streets.reduce(0) { $0 + 2 + 4 * $1 }
}

/// Builds the heading-up frame around the snapped position. `maxBytes` is the largest
/// frame the link can deliver (see `chunkCapacity`): whole streets are dropped, lowest
/// priority first (class, then nearness to the rider), until it fits; the route's points
/// are never dropped, only simplified harder if the route alone is too big.
public func buildScene(progress: Progress, route: Route, streets: [StreetLine], state: NavState,
                       speed: Double = 0, maxBytes: Int = NavLimits.maxFrameBytes) -> NavScene {
    let next = route.instructions.indices.contains(progress.nextInstruction) ? route.instructions[progress.nextInstruction] : nil
    let rejoin = progress.offRoute || state == .offRouteNoSignal
    let ended = state == .arrived || state == .ended
    let dist = ended ? "" : formatDistance(rejoin ? progress.distanceToRoute : progress.distanceToTurn)
    let eta = ended ? "" : formatETA(progress.remainingTime)
    let streetName = next?.streetName ?? ""
    let turn: NavTurn = state == .arrived ? .arrive : (rejoin ? .rejoin : (next?.turn ?? .straight))
    var dir: UInt8 = 0
    if rejoin {
        let rel = ((progress.bearingToRoute - progress.heading).truncatingRemainder(dividingBy: 360) + 360).truncatingRemainder(dividingBy: 360)
        dir = UInt8(Int((rel / 360 * 256).rounded()) & 0xff)
    }
    var scene = NavScene(state: state, turn: turn, exit: rejoin ? 0 : (next?.exit ?? 0), dir: dir,
                         gpsWeak: progress.gpsWeak || progress.headingGuess, dist: dist, street: streetName, eta: eta, route: [], streets: [])
    if ended { return scene }

    let mpp = rejoin ? min(3.0, max(1.5, progress.distanceToRoute / 100))
                     : metersPerPixel(distanceToTurn: progress.distanceToTurn, speed: speed)
    let h = progress.heading * .pi / 180
    let sinH = sin(h), cosH = cos(h)
    let center = progress.fix
    let kx = metersPerDegree * cos(center.lat * .pi / 180)
    func project(_ c: Coord) -> P {
        let e = (c.lon - center.lon) * kx, n = (c.lat - center.lat) * metersPerDegree
        let fwd = e * sinH + n * cosH, right = e * cosH - n * sinH
        return P(x: riderX + right / mpp, y: riderY - fwd / mpp)
    }

    // Route: from the snapped position forward, culled to what can reach the box. Of the
    // visible runs, the one nearest the rider (the first on route, where it starts at the rider).
    let radius = 550 * mpp   // about the clip box's diagonal in meters
    var path = [project(progress.snapped)]
    var along = 0.0
    for i in route.points.indices {
        if i > 0 { along += distanceMeters(route.points[i - 1], route.points[i]) }
        if along > progress.distanceAlong + 0.01 {
            path.append(project(route.points[i]))
            if hypot(path[path.count - 1].x - riderX, path[path.count - 1].y - riderY) * mpp > radius * 1.5 { break }
        }
    }
    let routeRun = clip(path).min { runDist($0) < runDist($1) } ?? []
    let fixed = frameBytes(dist: dist, street: streetName, eta: eta, route: 0, streets: [])
    var eps = 1.5
    var routePts = round16(douglasPeucker(routeRun, epsilon: eps))
    while (routePts.count > NavLimits.maxRoute || fixed + 4 * routePts.count > maxBytes) && eps < 4096 {
        eps *= 2
        routePts = round16(douglasPeucker(routeRun, epsilon: eps))
    }
    scene.route = routePts.count >= 2 ? routePts : []

    // Streets: clip, simplify, order by priority, keep each in turn that still fits.
    struct Cand { var cls: UInt8; var pts: [ScreenPoint]; var near: Double; var idx: Int }
    var cands: [Cand] = []
    let dLat = radius / metersPerDegree, dLon = radius / max(1, kx)
    for (idx, s) in streets.enumerated() where s.points.count >= 2 && s.cls <= 3 {
        let b = s.bounds
        if b.maxLat < center.lat - dLat || b.minLat > center.lat + dLat
            || b.maxLon < center.lon - dLon || b.minLon > center.lon + dLon { continue }
        for run in clip(s.points.map(project)) {
            let pts = round16(douglasPeucker(run, epsilon: 1.5))
            guard pts.count >= 2 else { continue }
            let near = pts.reduce(Double.infinity) { m, p in
                min(m, ((Double(p.x) - riderX) * (Double(p.x) - riderX) + (Double(p.y) - riderY) * (Double(p.y) - riderY)))
            }
            var i = 0   // a run over 255 points is cut into overlapping pieces
            while i < pts.count - 1 {
                let end = min(pts.count, i + 255)
                cands.append(Cand(cls: s.cls, pts: Array(pts[i..<end]), near: near, idx: idx))
                i = end - 1
            }
        }
    }
    cands.sort { $0.cls != $1.cls ? $0.cls < $1.cls : ($0.near != $1.near ? $0.near < $1.near : $0.idx < $1.idx) }
    var used = fixed + 4 * scene.route.count
    var total = 0
    for c in cands {
        if scene.streets.count >= NavLimits.maxStreets { break }
        // A street that does not fit is skipped; a smaller one after it may still fit.
        let cost = 2 + 4 * c.pts.count
        if total + c.pts.count > NavLimits.maxStreetPoints || used + cost > maxBytes { continue }
        scene.streets.append(SceneStreet(cls: c.cls, points: c.pts))
        used += cost; total += c.pts.count
    }
    return scene
}

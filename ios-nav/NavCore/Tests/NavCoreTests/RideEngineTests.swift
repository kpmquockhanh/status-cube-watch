import Testing
import Foundation
@testable import NavCore

private let origin = Coord(lat: 50, lon: 10)
private let mPerDegLat = 6_371_000.0 * .pi / 180
private func north(_ m: Double, east e: Double = 0, from c: Coord = origin) -> Coord {
    Coord(lat: c.lat + m / mPerDegLat, lon: c.lon + e / (mPerDegLat * cos(c.lat * .pi / 180)))
}

/// 500 m north, then 300 m east.
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

private actor FakeNavServer: NavServer {
    var online = true
    var fixedRoute: Route?
    var routeCalls = 0
    var tileCalls = 0
    var hangTiles = false
    var hangRoute = false
    var failTiles = false
    /// Tiles with an odd x fail (a patchy connection).
    var failOddTiles = false
    /// Tile requests wait while this is set.
    var holdTiles = false
    var lastTo: Coord?
    func set(hangTiles: Bool) { self.hangTiles = hangTiles }
    func set(hangRoute: Bool) { self.hangRoute = hangRoute }
    func set(failTiles: Bool) { self.failTiles = failTiles }
    func set(failOddTiles: Bool) { self.failOddTiles = failOddTiles }
    func set(holdTiles: Bool) { self.holdTiles = holdTiles }
    func set(online: Bool) { self.online = online }
    func set(route: Route?) { fixedRoute = route }

    func route(from: Coord, to: Coord) async throws -> Route {
        routeCalls += 1
        lastTo = to
        guard online else { throw StartError.noSignal }
        if hangRoute { try await Task.sleep(nanoseconds: 3_600_000_000_000) }
        if let fixedRoute { return fixedRoute }
        return Route(points: [from, to], distance: distanceMeters(from, to), time: 60, instructions: [
            Instruction(turn: .straight, streetName: "New", distance: distanceMeters(from, to), time: 60, interval: 0...1),
            Instruction(turn: .arrive, streetName: "", distance: 0, time: 0, interval: 1...1)])
    }

    func tile(_ id: TileID) async throws -> [StreetLine] {
        tileCalls += 1
        while holdTiles { try await Task.sleep(nanoseconds: 2_000_000) }
        guard online, !failTiles, !(failOddTiles && id.x % 2 != 0) else { throw StartError.noSignal }
        if hangTiles { try await Task.sleep(nanoseconds: 3_600_000_000_000) }
        // One cross street through the corridor (west-east at 250 m north), and one street in the
        // tile itself so tiles can be told apart.
        let c = tileCenter(id)
        return [StreetLine(cls: 2, points: [north(250, east: -300), north(250, east: 300)]),
                StreetLine(cls: 3, points: [north(-20, east: 0, from: c), north(20, east: 0, from: c)])]
    }
}

/// The middle of a zoom-15 tile.
private func tileCenter(_ id: TileID) -> Coord {
    let n = Double(1 << 15)
    let lon = (Double(id.x) + 0.5) / n * 360 - 180
    let lat = atan(sinh(.pi * (1 - 2 * (Double(id.y) + 0.5) / n))) * 180 / .pi
    return Coord(lat: lat, lon: lon)
}

private final class Clock: @unchecked Sendable {
    var t = Date(timeIntervalSince1970: 1_000_000)
    func advance(_ s: TimeInterval) { t = t.addingTimeInterval(s) }
}

private func tmpCache() -> RideCache {
    RideCache(directory: FileManager.default.temporaryDirectory.appendingPathComponent("ride-\(UUID().uuidString)"))
}

@MainActor private func makeEngine(_ server: FakeNavServer, _ clock: Clock, cache: RideCache = tmpCache(), timeout: TimeInterval = 8) -> RideEngine {
    RideEngine(server: server, cache: cache, now: { clock.t }, rerouteTimeout: timeout)
}

@MainActor @Test func startOfflineThrowsNoSignalAndStaysEnded() async {
    let server = FakeNavServer(); await server.set(online: false)
    let engine = makeEngine(server, Clock())
    await #expect(throws: StartError.noSignal) { try await engine.start(from: north(0), to: north(0, east: 300, from: north(500))) }
    #expect(engine.state == .ended)
    #expect(engine.update(fix(north(10))).state == .ended)
}

@MainActor @Test func rideNavigatesThenArrivesThenEnds() async throws {
    let server = FakeNavServer(); await server.set(route: lRoute())
    let clock = Clock()
    let engine = makeEngine(server, clock)
    try await engine.start(from: north(0), to: lRoute().points.last!)
    #expect(engine.state == .navigating)
    #expect(await server.tileCalls > 0)

    var seen: [NavScene] = []
    for m in stride(from: 10.0, through: 480, by: 40) { seen.append(engine.update(fix(north(m)))) }
    for e in stride(from: 20.0, through: 220, by: 40) { seen.append(engine.update(fix(north(0, east: e, from: north(500))))) }
    #expect(seen.allSatisfy { $0.state == .navigating })
    #expect(!seen[0].route.isEmpty)
    #expect(!seen[0].streets.isEmpty)
    #expect(seen[0].turn == .right)

    let arrived = engine.update(fix(lRoute().points.last!))
    #expect(arrived.state == .arrived)
    #expect(arrived.turn == .arrive)
    clock.advance(7)
    #expect(engine.update(fix(lRoute().points.last!)).state == .arrived)
    clock.advance(1.5)
    #expect(engine.update(fix(lRoute().points.last!)).state == .ended)
}

@MainActor @Test func offRouteOfflineShowsRejoinWithStreetsThenReroutesWhenBack() async throws {
    let server = FakeNavServer(); await server.set(route: lRoute())
    let clock = Clock()
    let engine = makeEngine(server, clock)
    let dest = lRoute().points.last!
    try await engine.start(from: north(0), to: dest)
    engine.frameBudget = 4096
    await server.set(online: false); await server.set(route: nil)

    _ = engine.update(fix(north(200)))
    let off = north(250, east: -120)
    _ = engine.update(fix(off)); _ = engine.update(fix(off))
    let third = engine.update(fix(off))
    #expect(third.state == .rerouting)
    await engine.waitForReroute()

    let lost = engine.update(fix(off))
    #expect(lost.state == .offRouteNoSignal)
    #expect(lost.turn == .rejoin)
    #expect(!lost.streets.isEmpty)
    #expect(await server.routeCalls == 2)

    // Not yet 15 s: no new attempt.
    clock.advance(10)
    #expect(engine.update(fix(off)).state == .offRouteNoSignal)
    #expect(await server.routeCalls == 2)

    await server.set(online: true)
    clock.advance(5)
    // A retry keeps the "no signal" label while in flight.
    #expect(engine.update(fix(off)).state == .offRouteNoSignal)
    await engine.waitForReroute()
    #expect(engine.state == .navigating)
    let back = engine.update(fix(north(0, east: 0, from: off)))
    #expect(back.state == .navigating)
    #expect(await server.routeCalls == 3)
}

@MainActor @Test func resumeLastRestoresTheCachedRide() async throws {
    let server = FakeNavServer(); await server.set(route: lRoute())
    let cache = tmpCache()
    let first = makeEngine(server, Clock(), cache: cache)
    try await first.start(from: north(0), to: lRoute().points.last!)
    // No explicit wait: resumeLast waits for the write still in flight.
    let engine = makeEngine(server, Clock(), cache: cache)
    #expect(await engine.resumeLast())
    let scene = engine.update(fix(north(100)))
    #expect(scene.state == .navigating)
    #expect(!scene.streets.isEmpty)
    #expect(RideCache(directory: FileManager.default.temporaryDirectory.appendingPathComponent("none-\(UUID())")).loadLast() == nil)
}

@MainActor @Test func resumeLastUsesTheStoredDestination() async throws {
    let server = FakeNavServer(); await server.set(route: lRoute())
    let cache = tmpCache()
    // The rider asked for a place 300 m past where the route ends (e.g. inside a car-free area).
    let asked = north(300, east: 0, from: lRoute().points.last!)
    let first = makeEngine(server, Clock(), cache: cache)
    try await first.start(from: north(0), to: asked)
    await first.waitForSave()
    let saved = try #require(cache.loadLast())
    #expect(distanceMeters(saved.destination, asked) < 1)

    let engine = makeEngine(server, Clock(), cache: cache)
    #expect(await !engine.resumeLast(near: lRoute().points.last!))   // 300 m off: not this trip
    #expect(await engine.resumeLast(near: north(50, east: 0, from: asked)))
    #expect(await cache.hasRide(near: asked))
    // A reroute from the resumed ride heads for the asked place, not the old route's end.
    await server.set(route: nil)
    _ = engine.update(fix(north(200)))
    let off = north(250, east: -120)
    _ = engine.update(fix(off)); _ = engine.update(fix(off)); _ = engine.update(fix(off))
    await engine.waitForReroute()
    let to = try #require(await server.lastTo)
    #expect(distanceMeters(to, asked) < 1)
}

@Test func cacheRoundTripsAndRejectsOtherVersions() throws {
    let dir = FileManager.default.temporaryDirectory.appendingPathComponent("ride-\(UUID().uuidString)")
    let cache = RideCache(directory: dir)
    let route = lRoute()
    let tiles = [TileID(x: 3, y: 4): [StreetLine(cls: 1, points: [north(10), north(20, east: 5)])], TileID(x: 5, y: 6): []]
    let dest = north(0, east: 310, from: north(500))
    cache.save(route: route, tiles: tiles, destination: dest)
    let back = try #require(cache.loadLast())
    #expect(back.route.points.count == route.points.count)
    #expect(zip(back.route.points, route.points).allSatisfy { distanceMeters($0, $1) < 0.2 })
    #expect(back.route.instructions == route.instructions)
    #expect(back.route.distance == route.distance && back.route.time == route.time)
    #expect(Set(back.tiles.keys) == Set(tiles.keys))
    #expect(back.tiles[TileID(x: 3, y: 4)]?.first?.cls == 1)
    #expect(distanceMeters(back.destination, dest) < 0.2)
    // Integer microdegrees, not decimal degrees.
    let text = try String(contentsOf: dir.appendingPathComponent("last-ride.json"), encoding: .utf8)
    #expect(text.contains("\"v\":2"))
    #expect(!text.contains("\"lat\""))

    // Another version (or the old layout without one) reads as no ride.
    let file = dir.appendingPathComponent("last-ride.json")
    try Data(text.replacingOccurrences(of: "\"v\":2", with: "\"v\":1").utf8).write(to: file)
    #expect(cache.loadLast() == nil)
    try Data(#"{"points":[{"lat":1,"lon":2}],"distance":1,"time":1,"instructions":[],"tiles":[]}"#.utf8).write(to: file)
    #expect(cache.loadLast() == nil)
}

@MainActor private func offRouteEngine(_ server: FakeNavServer, _ clock: Clock, timeout: TimeInterval = 8) async throws -> (RideEngine, Coord) {
    await server.set(route: lRoute())
    let engine = makeEngine(server, clock, timeout: timeout)
    try await engine.start(from: north(0), to: lRoute().points.last!)
    // The reroute detours 2 km west, so it needs corridor tiles the first route never loaded.
    let far = north(250, east: -2000)
    await server.set(route: Route(points: [north(250, east: -120), far, lRoute().points.last!], distance: 4000, time: 400, instructions: [
        Instruction(turn: .straight, streetName: "Detour", distance: 4000, time: 400, interval: 0...2),
        Instruction(turn: .arrive, streetName: "", distance: 0, time: 0, interval: 2...2)]))
    _ = engine.update(fix(north(200)))
    return (engine, north(250, east: -120))
}

@MainActor @Test func hangingRouteTimesOutKeepsOldRideAndRetriesLater() async throws {
    let server = FakeNavServer(); let clock = Clock()
    let (engine, off) = try await offRouteEngine(server, clock, timeout: 0.05)
    await server.set(hangRoute: true)
    _ = engine.update(fix(off)); _ = engine.update(fix(off))
    #expect(engine.update(fix(off)).state == .rerouting)
    await engine.waitForReroute()
    let lost = engine.update(fix(off))
    #expect(lost.state == .offRouteNoSignal)
    #expect(!lost.streets.isEmpty)
    #expect(engine.route == lRoute())
    await server.set(hangRoute: false)
    clock.advance(15)
    _ = engine.update(fix(off))
    await engine.waitForReroute()
    #expect(engine.state == .navigating)
    #expect(engine.route != lRoute())
}

@MainActor @Test func slowTilesDoNotHoldBackTheNewRoute() async throws {
    let server = FakeNavServer(); let clock = Clock()
    let (engine, off) = try await offRouteEngine(server, clock, timeout: 0.5)
    let held = engine.tiles
    await server.set(holdTiles: true)
    _ = engine.update(fix(off)); _ = engine.update(fix(off)); _ = engine.update(fix(off))
    let t0 = Date()
    await engine.waitForReroute()
    #expect(Date().timeIntervalSince(t0) < 0.5)   // within the deadline, tiles still pending
    #expect(engine.state == .navigating)
    #expect(engine.route != lRoute())
    #expect(engine.tiles == held)                  // drawn with what was held meanwhile
    #expect(!engine.missingTiles.isEmpty)
    let farTile = tile(for: north(250, east: -2000))
    #expect(engine.missingTiles.contains(farTile))

    // The tiles arrive later and show up in the scenes from then on.
    let before = engine.update(fix(north(250, east: -1990))).streets.count
    await server.set(holdTiles: false)
    await engine.waitForTiles()
    #expect(engine.missingTiles.isEmpty)
    #expect(engine.tiles[farTile] != nil)
    let after = engine.update(fix(north(250, east: -1990)))
    #expect(after.state == .navigating)
    #expect(after.streets.count > before)
}

@MainActor @Test func tilesFromAnEarlierAttemptAreKept() async throws {
    let server = FakeNavServer(); let clock = Clock()
    let (engine, off) = try await offRouteEngine(server, clock)
    let before = engine.tiles
    await server.set(failOddTiles: true)
    _ = engine.update(fix(off)); _ = engine.update(fix(off)); _ = engine.update(fix(off))
    await engine.waitForReroute()
    await engine.waitForTiles()
    #expect(await server.routeCalls == 2)
    #expect(engine.state == .navigating)
    #expect(engine.route != lRoute())
    // Old tiles stay; the even tiles that came are kept; only the odd ones are missing.
    #expect(before.keys.allSatisfy { engine.tiles[$0] == before[$0] })
    #expect(engine.tiles.count > before.count)
    #expect(!engine.missingTiles.isEmpty && engine.missingTiles.allSatisfy { $0.x % 2 != 0 })

    // Later only the missing ones are asked for, not every corridor tile again.
    await server.set(failOddTiles: false)
    let calls = await server.tileCalls
    let missing = engine.missingTiles.count
    clock.advance(10)
    _ = engine.tick()
    #expect(engine.tileTask == nil)                // not yet 15 s
    clock.advance(5)
    _ = engine.tick()
    await engine.waitForTiles()
    #expect(await server.tileCalls == calls + missing)
    #expect(engine.missingTiles.isEmpty)
}

@MainActor @Test func stopWhileReroutingIsNotRevivedByTheLateResult() async throws {
    let server = FakeNavServer(); let clock = Clock()
    let (engine, off) = try await offRouteEngine(server, clock)
    _ = engine.update(fix(off)); _ = engine.update(fix(off))
    #expect(engine.update(fix(off)).state == .rerouting)
    let task = engine.rerouteTask
    #expect(engine.stop().state == .ended)
    await task?.value
    #expect(engine.state == .ended)
    #expect(engine.update(fix(off)).state == .ended)
}

// MARK: tick (time passing without a new fix)

@MainActor @Test func ticksNeverCountOneFixTwiceTowardOffRoute() async throws {
    let server = FakeNavServer(); await server.set(route: lRoute())
    let clock = Clock()
    let engine = makeEngine(server, clock)
    try await engine.start(from: north(0), to: lRoute().points.last!)
    _ = engine.update(fix(north(200)))
    // One bad reading 120 m off the route, then the phone hears nothing for a while.
    let scene = engine.update(fix(north(250, east: -120)))
    #expect(scene.state == .navigating)
    for _ in 0..<5 {
        clock.advance(1)
        let s = try #require(engine.tick())
        #expect(s.state == .navigating)
        #expect(s.turn != .rejoin)
    }
    #expect(engine.rerouteTask == nil)
    #expect(await server.routeCalls == 1)
    // The next real fix is back on the route: still navigating.
    #expect(engine.update(fix(north(260))).state == .navigating)
}

@MainActor @Test func tickEndsTheArrivalHoldWithoutFixes() async throws {
    let server = FakeNavServer(); await server.set(route: lRoute())
    let clock = Clock()
    let engine = makeEngine(server, clock)
    try await engine.start(from: north(0), to: lRoute().points.last!)
    #expect(engine.tick() == nil)   // no fix yet: nothing to draw
    for m in stride(from: 10.0, through: 480, by: 40) { _ = engine.update(fix(north(m))) }
    for e in stride(from: 20.0, through: 220, by: 40) { _ = engine.update(fix(north(0, east: e, from: north(500)))) }
    #expect(engine.update(fix(lRoute().points.last!)).state == .arrived)
    clock.advance(4)
    #expect(engine.tick()?.state == .arrived)
    clock.advance(4)
    #expect(engine.tick()?.state == .ended)
    #expect(engine.state == .ended)
}

@MainActor @Test func tickRetriesTheRerouteEvery15sWithoutFixes() async throws {
    let server = FakeNavServer(); await server.set(route: lRoute())
    let clock = Clock()
    let engine = makeEngine(server, clock)
    try await engine.start(from: north(0), to: lRoute().points.last!)
    await server.set(online: false); await server.set(route: nil)
    _ = engine.update(fix(north(200)))
    let off = north(250, east: -120)
    _ = engine.update(fix(off)); _ = engine.update(fix(off))
    #expect(engine.update(fix(off)).state == .rerouting)
    await engine.waitForReroute()
    #expect(engine.state == .offRouteNoSignal)
    #expect(await server.routeCalls == 2)

    // No more fixes (phone in a pocket under a bridge); only ticks.
    clock.advance(10)
    #expect(engine.tick()?.state == .offRouteNoSignal)
    #expect(engine.rerouteTask == nil)
    await server.set(online: true)
    clock.advance(5)
    let s = try #require(engine.tick())
    #expect(s.state == .offRouteNoSignal)   // in flight, label unchanged
    #expect(s.gpsWeak)                      // the last fix is 15 s old
    await engine.waitForReroute()
    #expect(await server.routeCalls == 3)
    #expect(engine.state == .navigating)
    #expect(engine.tick()?.state == .navigating)
}

@MainActor @Test func tickMarksAnOldFixWeak() async throws {
    let server = FakeNavServer(); await server.set(route: lRoute())
    let clock = Clock()
    let engine = makeEngine(server, clock)
    try await engine.start(from: north(0), to: lRoute().points.last!)
    #expect(!engine.update(fix(north(100))).gpsWeak)
    clock.advance(4)
    #expect(engine.tick()?.gpsWeak == false)
    clock.advance(2)
    #expect(engine.tick()?.gpsWeak == true)
    #expect(!engine.update(fix(north(110))).gpsWeak)
}

// MARK: HTTPNavServer against a stub URLProtocol

private final class Stub: URLProtocol {
    nonisolated(unsafe) static var handler: ((URLRequest, Data?) -> (Int, Data))?
    nonisolated(unsafe) static var seen: [(URLRequest, Data?)] = []
    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        var body = request.httpBody
        if body == nil, let s = request.httpBodyStream {
            s.open(); var d = Data(); var buf = [UInt8](repeating: 0, count: 4096)
            while s.hasBytesAvailable { let n = s.read(&buf, maxLength: buf.count); if n <= 0 { break }; d.append(buf, count: n) }
            s.close(); body = d
        }
        Self.seen.append((request, body))
        let (code, data) = Self.handler!(request, body)
        let resp = HTTPURLResponse(url: request.url!, statusCode: code, httpVersion: nil, headerFields: nil)!
        client?.urlProtocol(self, didReceive: resp, cacheStoragePolicy: .notAllowed)
        client?.urlProtocol(self, didLoad: data)
        client?.urlProtocolDidFinishLoading(self)
    }
    override func stopLoading() {}
}

private final class Sleeps: @unchecked Sendable { var list: [TimeInterval] = [] }

private func httpServer(_ sleeps: Sleeps = Sleeps()) -> HTTPNavServer {
    let cfg = URLSessionConfiguration.ephemeral
    cfg.protocolClasses = [Stub.self]
    Stub.seen = []
    return HTTPNavServer(baseURL: URL(string: "https://nav.example")!, key: "K3Y", session: URLSession(configuration: cfg),
                         sleep: { sleeps.list.append($0) })
}

private let tileJSON = Data(#"{"v":1,"z":15,"x":1,"y":2,"w":[[2,50000000,10000000,50001000,10001000]]}"#.utf8)

@Suite(.serialized) struct HTTPNavServerTests {
    @Test func routeSendsKeyAndExactBody() async throws {
        let json = try Data(contentsOf: #require(Bundle.module.url(forResource: "route-hcmc", withExtension: "json", subdirectory: "Fixtures")))
        let server = httpServer()
        Stub.handler = { _, _ in (200, json) }
        let r = try await server.route(from: Coord(lat: 10.5, lon: 106.5), to: Coord(lat: 10.6, lon: 106.6))
        #expect(r.points.count > 2)
        let (req, body) = try #require(Stub.seen.first)
        #expect(req.httpMethod == "POST")
        #expect(req.url?.path == "/route")
        #expect(req.value(forHTTPHeaderField: "X-Nav-Key") == "K3Y")
        #expect(String(decoding: body ?? Data(), as: UTF8.self) ==
            #"{"points":[[106.5,10.5],[106.6,10.6]],"profile":"moto_vn","instructions":true,"points_encoded":false,"locale":"en","details":["road_class"]}"#)
    }

    @Test func tile404IsEmptyAnd200Decodes() async throws {
        let server = httpServer()
        Stub.handler = { req, _ in req.url!.path.hasSuffix("/2.json") ? (200, tileJSON) : (404, Data()) }
        #expect(try await server.tile(TileID(x: 1, y: 3)).isEmpty)
        #expect(try await server.tile(TileID(x: 1, y: 2)).count == 1)
        let (req, _) = try #require(Stub.seen.first)
        #expect(req.url?.path == "/tiles/15/1/3.json")
        #expect(req.value(forHTTPHeaderField: "X-Nav-Key") == "K3Y")
    }

    @Test func unauthorizedIsBadKeyAndServerErrorCarriesCode() async {
        let server = httpServer()
        Stub.handler = { _, _ in (401, Data()) }
        await #expect(throws: StartError.badKey) { try await server.tile(TileID(x: 1, y: 2)) }
        Stub.handler = { _, _ in (500, Data()) }
        await #expect(throws: StartError.server(500)) { try await server.tile(TileID(x: 1, y: 2)) }
    }

    @Test func rateLimitIsRetriedWithBackoff() async throws {
        let sleeps = Sleeps()
        let server = httpServer(sleeps)
        nonisolated(unsafe) var n = 0
        Stub.handler = { _, _ in n += 1; return n == 1 ? (429, Data()) : (200, tileJSON) }
        #expect(try await server.tile(TileID(x: 1, y: 2)).count == 1)
        #expect(sleeps.list == [0.5])
        n = -100   // always 503
        Stub.handler = { _, _ in (503, Data()) }
        sleeps.list = []
        await #expect(throws: StartError.server(503)) { try await server.tile(TileID(x: 1, y: 2)) }
        #expect(sleeps.list == [0.5, 1, 2, 4])
    }

    @Test func transportFailureIsNoSignal() async {
        let server = httpServer()
        Stub.handler = nil
        // URLProtocol with no handler would crash: use an unroutable stub error instead.
        final class Fail: URLProtocol, @unchecked Sendable {
            override class func canInit(with request: URLRequest) -> Bool { true }
            override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
            override func startLoading() { client?.urlProtocol(self, didFailWithError: URLError(.notConnectedToInternet)) }
            override func stopLoading() {}
        }
        let cfg = URLSessionConfiguration.ephemeral; cfg.protocolClasses = [Fail.self]
        let s = HTTPNavServer(baseURL: URL(string: "https://nav.example")!, key: "k", session: URLSession(configuration: cfg))
        _ = server
        await #expect(throws: StartError.noSignal) { try await s.tile(TileID(x: 1, y: 2)) }
    }
}

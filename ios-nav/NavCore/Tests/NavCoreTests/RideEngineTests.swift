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
    var failTiles = false
    func set(hangTiles: Bool) { self.hangTiles = hangTiles }
    func set(failTiles: Bool) { self.failTiles = failTiles }
    func set(online: Bool) { self.online = online }
    func set(route: Route?) { fixedRoute = route }

    func route(from: Coord, to: Coord) async throws -> Route {
        routeCalls += 1
        guard online else { throw StartError.noSignal }
        if let fixedRoute { return fixedRoute }
        return Route(points: [from, to], distance: distanceMeters(from, to), time: 60, instructions: [
            Instruction(turn: .straight, streetName: "New", distance: distanceMeters(from, to), time: 60, interval: 0...1),
            Instruction(turn: .arrive, streetName: "", distance: 0, time: 0, interval: 1...1)])
    }

    func tile(_ id: TileID) async throws -> [StreetLine] {
        tileCalls += 1
        guard online, !failTiles else { throw StartError.noSignal }
        if hangTiles { try await Task.sleep(nanoseconds: 3_600_000_000_000) }
        // One cross street through the corridor (west-east at 250 m north).
        return [StreetLine(cls: 2, points: [north(250, east: -300), north(250, east: 300)])]
    }
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
    await first.waitForSave()
    let engine = makeEngine(server, Clock(), cache: cache)
    #expect(engine.resumeLast())
    let scene = engine.update(fix(north(100)))
    #expect(scene.state == .navigating)
    #expect(!scene.streets.isEmpty)
    #expect(RideCache(directory: FileManager.default.temporaryDirectory.appendingPathComponent("none-\(UUID())")).loadLast() == nil)
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

@MainActor @Test func hangingTilesTimeOutKeepOldRideAndRetryLater() async throws {
    let server = FakeNavServer(); let clock = Clock()
    let (engine, off) = try await offRouteEngine(server, clock, timeout: 0.05)
    await server.set(hangTiles: true)
    _ = engine.update(fix(off)); _ = engine.update(fix(off))
    #expect(engine.update(fix(off)).state == .rerouting)
    await engine.waitForReroute()
    let lost = engine.update(fix(off))
    #expect(lost.state == .offRouteNoSignal)
    #expect(!lost.streets.isEmpty)
    #expect(engine.route == lRoute())
    await server.set(hangTiles: false)
    clock.advance(15)
    _ = engine.update(fix(off))
    await engine.waitForReroute()
    #expect(engine.state == .navigating)
    #expect(engine.route != lRoute())
}

@MainActor @Test func rerouteWithFailedTilesKeepsOldRouteAndTiles() async throws {
    let server = FakeNavServer(); let clock = Clock()
    let (engine, off) = try await offRouteEngine(server, clock)
    let tilesBefore = engine.tiles
    await server.set(failTiles: true)
    _ = engine.update(fix(off)); _ = engine.update(fix(off)); _ = engine.update(fix(off))
    await engine.waitForReroute()
    #expect(await server.routeCalls == 2)   // the route request itself succeeded
    #expect(engine.state == .offRouteNoSignal)
    #expect(engine.route == lRoute())
    #expect(engine.tiles == tilesBefore)
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

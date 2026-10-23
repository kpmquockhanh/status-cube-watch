import Foundation

/// Offline-first ride: everything needed is fetched in `start`; after that `update` and `tick`
/// never wait for the network. Off-route it may ask the server for a new route in the background;
/// the new route is used as soon as it arrives, and its missing corridor tiles follow.
@MainActor public final class RideEngine {
    static let arrivedHold: TimeInterval = 8      // the cube's NAV_ARRIVED_MS
    static let rerouteRetry: TimeInterval = 15
    /// Missing corridor tiles (after a reroute) are asked for again this often.
    static let tileRetry: TimeInterval = 15
    /// A last fix older than this is shown as weak GPS.
    static let fixStale: TimeInterval = 5
    nonisolated static let concurrentTiles = 8

    private let server: NavServer
    private let cache: RideCache
    private let now: () -> Date
    private let sleep: @Sendable (TimeInterval) async throws -> Void
    private let rerouteTimeout: TimeInterval

    public private(set) var state: NavState = .ended
    /// Largest frame the link can deliver; the BLE layer sets `chunkCapacity(maxWrite:)`.
    public var frameBudget: Int = NavLimits.maxFrameBytes

    private(set) var route: Route?
    /// The style the rider picked; every reroute asks for it again (Calm by the clock at that moment).
    public private(set) var style: RouteStyle = .fastest
    private var tracker: RouteTracker?
    private var destination: Coord?
    private(set) var tiles: [TileID: [StreetLine]] = [:]
    private var streets: [StreetLine] = []
    private var arrivedAt: Date?
    private var lastRerouteAttempt: Date?
    private(set) var rerouteTask: Task<Void, Never>?
    private var generation = 0

    /// The last real fix, when it came, and what the tracker made of it.
    private var lastFix: Fix?
    private var lastFixAt: Date?
    private var lastProgress: Progress?

    /// Corridor tiles of the current route that are not held yet (only after a reroute).
    private(set) var missingTiles: Set<TileID> = []
    private(set) var tileTask: Task<Void, Never>?
    private var tileGeneration = 0
    private var lastTileAttempt: Date?
    /// Bumped when a ride starts or stops: tiles fetched for another ride are not merged.
    private var rideID = 0

    public init(server: NavServer, cache: RideCache,
                now: @escaping () -> Date = Date.init,
                sleep: @escaping @Sendable (TimeInterval) async throws -> Void = { try await Task.sleep(nanoseconds: UInt64($0 * 1e9)) },
                rerouteTimeout: TimeInterval = 8) {
        self.server = server; self.cache = cache; self.now = now; self.sleep = sleep
        self.rerouteTimeout = rerouteTimeout
    }

    // MARK: start / stop

    /// Fetches the route and every corridor tile, or throws and keeps nothing.
    public func start(from: Coord, to: Coord, style: RouteStyle = .fastest) async throws {
        let (r, t) = try await fetch(from: from, to: to, profile: style.profile(rushHour: isRushHour(now())), have: [:])
        try Task.checkCancellation()
        newRide()
        begin(route: r, tiles: t, destination: to, style: style)
        save()
    }

    /// Rides a route the preview already has: fetches its corridor tiles only, or throws and keeps nothing.
    public func start(route r: Route, to: Coord, style: RouteStyle) async throws {
        let t = try await Self.fetchTiles(server: server, route: r, have: [:])
        try Task.checkCancellation()
        newRide()
        begin(route: r, tiles: t, destination: to, style: style)
        save()
    }

    /// Restarts the last cached ride (same trip, offline), after any cache write still in flight.
    /// With `near`, only when the cached ride's destination is within `within` meters of it.
    /// False when there is none.
    @discardableResult
    public func resumeLast(near dest: Coord? = nil, within meters: Double = 100) async -> Bool {
        await cache.waitForPendingSave()
        guard let c = cache.loadLast() else { return false }
        if let dest, distanceMeters(dest, c.destination) > meters { return false }
        newRide()
        begin(route: c.route, tiles: c.tiles, destination: c.destination, style: c.style)
        // A ride cached right after a reroute may lack some tiles: they are asked for in passing.
        missingTiles = corridorTiles(route: c.route.points).subtracting(c.tiles.keys)
        return true
    }

    public func stop() -> NavScene {
        newRide()
        state = .ended
        route = nil; tracker = nil; tiles = [:]; streets = []; arrivedAt = nil
        return emptyScene(.ended)
    }

    /// Forgets everything that belongs to the previous ride's network work.
    private func newRide() {
        cancelReroute()
        cancelTiles()
        rideID += 1
        missingTiles = []; lastTileAttempt = nil
        lastFix = nil; lastFixAt = nil; lastProgress = nil
    }

    private func begin(route r: Route, tiles t: [TileID: [StreetLine]], destination dest: Coord, style s: RouteStyle) {
        route = r; tracker = RouteTracker(route: r); destination = dest; style = s
        tiles = t; streets = t.values.flatMap { $0 }
        arrivedAt = nil; lastRerouteAttempt = nil
        state = .navigating
    }

    private func cancelReroute() {
        generation += 1
        rerouteTask?.cancel(); rerouteTask = nil
    }

    private func cancelTiles() {
        tileGeneration += 1
        tileTask?.cancel(); tileTask = nil
    }

    // MARK: update / tick

    /// A new GPS fix: tracks it, applies the state rules and builds the scene.
    public func update(_ fix: Fix) -> NavScene {
        guard let route, let tracker, state != .ended else { return emptyScene(.ended) }
        let t = now()
        let p = tracker.update(fix)
        lastFix = fix; lastFixAt = t; lastProgress = p

        if p.arrived || state == .arrived {
            if arrivedAt == nil { arrivedAt = t; cancelReroute() }
            state = t.timeIntervalSince(arrivedAt!) >= Self.arrivedHold ? .ended : .arrived
        } else if p.offRoute {
            if state == .navigating { requestReroute(from: p.fix, force: true, at: t) }
            else if state == .offRouteNoSignal { requestReroute(from: p.fix, force: false, at: t) }
        } else if state != .navigating {
            // Back on the route (rejoined by itself): forget the reroute.
            cancelReroute()
            state = .navigating
        }
        fetchMissingTilesIfDue(at: t)
        return buildScene(progress: p, route: route, streets: streets, state: state,
                          speed: fix.speed, maxBytes: frameBudget)
    }

    /// Time passing without a new fix: ends the arrival hold, retries a reroute every 15 s and
    /// rebuilds the scene from the last fix (weak GPS once it is older than 5 s). Never feeds the
    /// tracker, so one fix counts once toward off-route. nil before the ride's first fix.
    public func tick() -> NavScene? { tick(now: now()) }

    public func tick(now t: Date) -> NavScene? {
        guard let route, state != .ended else { return emptyScene(.ended) }
        guard var p = lastProgress else { return nil }
        if state == .arrived {
            if let a = arrivedAt, t.timeIntervalSince(a) >= Self.arrivedHold { state = .ended }
        } else if state == .offRouteNoSignal, p.offRoute {
            requestReroute(from: p.fix, force: false, at: t)
        }
        fetchMissingTilesIfDue(at: t)
        if let at = lastFixAt, t.timeIntervalSince(at) > Self.fixStale { p.gpsWeak = true }
        return buildScene(progress: p, route: route, streets: streets, state: state,
                          speed: lastFix?.speed ?? 0, maxBytes: frameBudget)
    }

    private func emptyScene(_ s: NavState) -> NavScene {
        NavScene(state: s, turn: .straight, exit: 0, dir: 0, gpsWeak: false, dist: "", street: "", eta: "",
                 route: [], streets: [])
    }

    // MARK: reroute

    private func requestReroute(from: Coord, force: Bool, at t: Date) {
        guard rerouteTask == nil, let destination else { return }
        if !force, let last = lastRerouteAttempt, t.timeIntervalSince(last) < Self.rerouteRetry { return }
        lastRerouteAttempt = t
        // Only the first attempt shows "Rerouting"; retries keep "no signal" so the label does not flip.
        if state == .navigating { state = .rerouting }
        generation += 1
        let gen = generation
        let server = self.server, sleep = self.sleep, limit = rerouteTimeout
        let profile = style.profile(rushHour: isRushHour(t))
        rerouteTask = Task { [weak self] in
            let result: Result<Route, Error>
            do { result = .success(try await Self.routeWithTimeout(server: server, from: from, to: destination,
                                                                   profile: profile, sleep: sleep, limit: limit)) }
            catch { result = .failure(error) }
            self?.finishReroute(result, generation: gen)
        }
    }

    /// The new route is used at once with the tiles already held; its missing tiles follow.
    private func finishReroute(_ result: Result<Route, Error>, generation gen: Int) {
        guard gen == generation else { return }   // superseded, rejoined, arrived or stopped
        rerouteTask = nil
        switch result {
        case .success(let r):
            begin(route: r, tiles: tiles, destination: destination ?? r.points.last ?? Coord(lat: 0, lon: 0), style: style)
            // The new tracker starts from the last fix (its first sight of it).
            lastProgress = lastFix.map { tracker!.update($0) }
            missingTiles = corridorTiles(route: r.points).subtracting(tiles.keys)
            lastTileAttempt = nil
            cancelTiles()
            fetchMissingTilesIfDue(at: now())
            save()
        case .failure:
            state = .offRouteNoSignal
        }
    }

    /// The deadline covers the route request only; on timeout it is a failure and the old route stays.
    nonisolated private static func routeWithTimeout(server: NavServer, from: Coord, to: Coord, profile: String,
                                                     sleep: @escaping @Sendable (TimeInterval) async throws -> Void,
                                                     limit: TimeInterval) async throws -> Route {
        try await withThrowingTaskGroup(of: Route.self) { group in
            group.addTask { try await server.route(from: from, to: to, profile: profile) }
            group.addTask { try await sleep(limit); throw StartError.noSignal }
            defer { group.cancelAll() }
            return try await group.next()!
        }
    }

    // MARK: tiles in the background

    /// Starts fetching the missing tiles, at most every 15 s, one fetch at a time.
    private func fetchMissingTilesIfDue(at t: Date) {
        guard tileTask == nil, !missingTiles.isEmpty, state != .ended else { return }
        if let last = lastTileAttempt, t.timeIntervalSince(last) < Self.tileRetry { return }
        lastTileAttempt = t
        tileGeneration += 1
        let gen = tileGeneration, ride = rideID, server = self.server
        let todo = missingTiles.sorted { ($0.x, $0.y) < ($1.x, $1.y) }
        tileTask = Task { [weak self] in
            await withTaskGroup(of: (TileID, [StreetLine]?).self) { group in
                var next = 0
                func add() {
                    guard next < todo.count else { return }
                    let id = todo[next]; next += 1
                    group.addTask { (id, try? await server.tile(id)) }
                }
                for _ in 0..<min(Self.concurrentTiles, todo.count) { add() }
                while let (id, lines) = await group.next() {
                    if let lines { self?.merge(id, lines, ride: ride) }
                    add()
                }
            }
            self?.tilesDone(generation: gen, ride: ride)
        }
    }

    /// Each tile is kept as it arrives, also from a fetch a later reroute superseded.
    private func merge(_ id: TileID, _ lines: [StreetLine], ride: Int) {
        guard ride == rideID, route != nil, tiles[id] == nil else { return }
        tiles[id] = lines
        streets.append(contentsOf: lines)
        missingTiles.remove(id)
    }

    private func tilesDone(generation gen: Int, ride: Int) {
        guard ride == rideID else { return }
        if gen == tileGeneration { tileTask = nil }
        save()
    }

    /// Writes the cache off the main actor, one write after another.
    private func save() {
        guard let route, let destination else { return }
        cache.saveInBackground(route: route, tiles: tiles, destination: destination, style: style)
    }

    // MARK: fetching

    private func fetch(from: Coord, to: Coord, profile: String, have: [TileID: [StreetLine]]) async throws -> (Route, [TileID: [StreetLine]]) {
        let server = self.server
        do {
            let r = try await server.route(from: from, to: to, profile: profile)
            return (r, try await Self.fetchTiles(server: server, route: r, have: have))
        } catch let e as StartError { throw e
        } catch let e as CancellationError { throw e
        } catch is URLError { throw StartError.noSignal
        } catch { throw StartError.server(0) }
    }

    /// All corridor tiles, 8 at a time; any failure cancels the rest and nothing is returned.
    nonisolated private static func fetchTiles(server: NavServer, route: Route, have: [TileID: [StreetLine]]) async throws -> [TileID: [StreetLine]] {
        let wanted = corridorTiles(route: route.points)
        var out: [TileID: [StreetLine]] = [:]
        var todo: [TileID] = []
        for id in wanted.sorted(by: { ($0.x, $0.y) < ($1.x, $1.y) }) {
            if let lines = have[id] { out[id] = lines } else { todo.append(id) }
        }
        do {
            try await withThrowingTaskGroup(of: (TileID, [StreetLine]).self) { group in
                var next = 0
                func add() {
                    guard next < todo.count else { return }
                    let id = todo[next]; next += 1
                    group.addTask { (id, try await server.tile(id)) }
                }
                for _ in 0..<min(concurrentTiles, todo.count) { add() }
                while let (id, lines) = try await group.next() { out[id] = lines; add() }
            }
        } catch let e as StartError { throw e
        } catch let e as CancellationError { throw e
        } catch is URLError { throw StartError.noSignal
        } catch { throw StartError.server(0) }
        return out
    }

    /// Test hooks: wait for a reroute, a tile fetch or cache writes in flight.
    func waitForReroute() async { await rerouteTask?.value }
    func waitForTiles() async { await tileTask?.value }
    func waitForSave() async { await cache.waitForPendingSave() }
}

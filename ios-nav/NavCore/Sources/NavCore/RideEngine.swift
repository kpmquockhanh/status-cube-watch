import Foundation

/// Offline-first ride: everything needed is fetched in `start`; after that `update` never
/// waits for the network. Off-route it may ask the server for a new route in the background.
@MainActor public final class RideEngine {
    static let arrivedHold: TimeInterval = 8      // the cube's NAV_ARRIVED_MS
    static let rerouteRetry: TimeInterval = 15
    static let concurrentTiles = 8

    private let server: NavServer
    private let cache: RideCache
    private let now: () -> Date
    private let sleep: @Sendable (TimeInterval) async throws -> Void
    private let rerouteTimeout: TimeInterval

    public private(set) var state: NavState = .ended
    /// Largest frame the link can deliver; the BLE layer sets `chunkCapacity(maxWrite:)`.
    public var frameBudget: Int = NavLimits.maxFrameBytes

    private(set) var route: Route?
    private var tracker: RouteTracker?
    private var destination: Coord?
    private(set) var tiles: [TileID: [StreetLine]] = [:]
    private var streets: [StreetLine] = []
    private var arrivedAt: Date?
    private var lastRerouteAttempt: Date?
    private(set) var rerouteTask: Task<Void, Never>?
    private var saveTask: Task<Void, Never>?
    private var generation = 0

    public init(server: NavServer, cache: RideCache,
                now: @escaping () -> Date = Date.init,
                sleep: @escaping @Sendable (TimeInterval) async throws -> Void = { try await Task.sleep(nanoseconds: UInt64($0 * 1e9)) },
                rerouteTimeout: TimeInterval = 8) {
        self.server = server; self.cache = cache; self.now = now; self.sleep = sleep
        self.rerouteTimeout = rerouteTimeout
    }

    // MARK: start / stop

    /// Fetches the route and every corridor tile, or throws and keeps nothing.
    public func start(from: Coord, to: Coord) async throws {
        let (r, t) = try await fetch(from: from, to: to, have: [:])
        cancelReroute()
        begin(route: r, tiles: t, destination: to)
        save(route: r, tiles: t)
    }

    /// Restarts the last cached ride (same trip, offline). False when there is none.
    @discardableResult
    public func resumeLast() -> Bool {
        guard let (r, t) = cache.loadLast(), let dest = r.points.last else { return false }
        cancelReroute()
        begin(route: r, tiles: t, destination: dest)
        return true
    }

    public func stop() -> NavScene {
        cancelReroute()
        state = .ended
        let scene = emptyScene(.ended)
        route = nil; tracker = nil; tiles = [:]; streets = []; arrivedAt = nil
        return scene
    }

    private func begin(route r: Route, tiles t: [TileID: [StreetLine]], destination dest: Coord) {
        route = r; tracker = RouteTracker(route: r); destination = dest
        tiles = t; streets = t.values.flatMap { $0 }
        arrivedAt = nil; lastRerouteAttempt = nil
        state = .navigating
    }

    private func cancelReroute() {
        generation += 1
        rerouteTask?.cancel(); rerouteTask = nil
    }

    // MARK: update

    public func update(_ fix: Fix) -> NavScene {
        guard let route, let tracker, state != .ended else { return emptyScene(.ended) }
        let p = tracker.update(fix)

        if p.arrived || state == .arrived {
            let t = now()
            if arrivedAt == nil { arrivedAt = t; cancelReroute() }
            state = t.timeIntervalSince(arrivedAt!) >= Self.arrivedHold ? .ended : .arrived
        } else if p.offRoute {
            if state == .navigating { requestReroute(from: p.fix, force: true) }
            else if state == .offRouteNoSignal { requestReroute(from: p.fix, force: false) }
        } else if state != .navigating {
            // Back on the route (rejoined by itself): forget the reroute.
            cancelReroute()
            state = .navigating
        }
        return buildScene(progress: p, route: route, streets: streets, state: state,
                          speed: fix.speed, maxBytes: frameBudget)
    }

    private func emptyScene(_ s: NavState) -> NavScene {
        NavScene(state: s, turn: .straight, exit: 0, dir: 0, gpsWeak: false, dist: "", street: "", eta: "",
                 route: [], streets: [])
    }

    // MARK: reroute

    private func requestReroute(from: Coord, force: Bool) {
        guard rerouteTask == nil, let destination else { return }
        if !force, let last = lastRerouteAttempt, now().timeIntervalSince(last) < Self.rerouteRetry { return }
        lastRerouteAttempt = now()
        // Only the first attempt shows "Rerouting"; retries keep "no signal" so the label does not flip.
        if state == .navigating { state = .rerouting }
        generation += 1
        let gen = generation
        rerouteTask = Task { [weak self] in
            guard let self else { return }
            let result: Result<(Route, [TileID: [StreetLine]]), Error>
            do { result = .success(try await self.fetchWithTimeout(from: from, to: destination)) }
            catch { result = .failure(error) }
            self.finishReroute(result, generation: gen)
        }
    }

    private func finishReroute(_ result: Result<(Route, [TileID: [StreetLine]]), Error>, generation gen: Int) {
        guard gen == generation else { return }   // superseded, rejoined, arrived or stopped
        rerouteTask = nil
        switch result {
        case .success(let (r, t)):
            begin(route: r, tiles: t, destination: destination ?? r.points.last ?? Coord(lat: 0, lon: 0))
            save(route: r, tiles: t)
        case .failure:
            state = .offRouteNoSignal
        }
    }

    /// The deadline covers the whole fetch (route and tiles): on timeout it is a failure and
    /// the old route and tiles stay.
    private func fetchWithTimeout(from: Coord, to: Coord) async throws -> (Route, [TileID: [StreetLine]]) {
        let server = self.server, have = tiles, sleep = self.sleep, limit = rerouteTimeout
        return try await withThrowingTaskGroup(of: (Route, [TileID: [StreetLine]]).self) { group in
            group.addTask {
                let r = try await server.route(from: from, to: to)
                return (r, try await Self.fetchTiles(server: server, route: r, have: have))
            }
            group.addTask { try await sleep(limit); throw StartError.noSignal }
            defer { group.cancelAll() }
            return try await group.next()!
        }
    }

    /// Writes the cache off the main actor, one write after another.
    private func save(route r: Route, tiles t: [TileID: [StreetLine]]) {
        let cache = self.cache, previous = saveTask
        saveTask = Task.detached(priority: .utility) {
            await previous?.value
            cache.save(route: r, tiles: t)
        }
    }

    // MARK: fetching

    private func fetch(from: Coord, to: Coord, have: [TileID: [StreetLine]]) async throws -> (Route, [TileID: [StreetLine]]) {
        let server = self.server
        do {
            let r = try await server.route(from: from, to: to)
            return (r, try await Self.fetchTiles(server: server, route: r, have: have))
        } catch let e as StartError { throw e
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
        } catch is URLError { throw StartError.noSignal
        } catch { throw StartError.server(0) }
        return out
    }

    /// Test hook: waits for a reroute in flight.
    func waitForReroute() async { await rerouteTask?.value }
    func waitForSave() async { await saveTask?.value }
}

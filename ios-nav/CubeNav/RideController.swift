import Combine
import Foundation
import NavCore

/// The app's long-lived state: owns the single `CubeLink` (created at launch, so iOS can restore
/// the Bluetooth link) and wires Location -> `RideEngine.update` -> `CubeLink.send`.
/// Frames go out at most 5 per second and at least 1 per second while riding.
@MainActor
final class RideController: ObservableObject {
    static let shared = RideController()

    enum Phase: Equatable { case idle, starting, riding }

    let cubeLink = CubeLink()
    let location = LocationService()

    @Published private(set) var phase: Phase = .idle
    @Published private(set) var scene: NavScene?
    @Published private(set) var message: String?
    /// Start failed for lack of signal and the cached ride goes to this destination.
    @Published private(set) var savedRouteAvailable = false

    static let minFrameGap: TimeInterval = 0.2
    static let maxFrameGap: TimeInterval = 1.0

    private var engine: RideEngine?
    private var latest: NavScene?
    private var dirty = false
    private var lastSent = Date.distantPast
    private var timer: DispatchSourceTimer?
    private var lastUpdate = Date.distantPast
    private var startTask: Task<Void, Never>?
    private var startToken = 0
    private var bag = Set<AnyCancellable>()
    /// One cache for every ride, so a resume waits for the last ride's write.
    private let cache = RideCache(directory: FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("ride"))

    private init() {
        cubeLink.$frameBudget.sink { [weak self] b in self?.engine?.frameBudget = b }.store(in: &bag)
        // Forward child changes so views that observe only the controller redraw.
        cubeLink.objectWillChange.sink { [weak self] in self?.objectWillChange.send() }.store(in: &bag)
        location.objectWillChange.sink { [weak self] in self?.objectWillChange.send() }.store(in: &bag)
        location.onFix = { [weak self] f in self?.handle(f) }
    }

    func clearMessage() { message = nil; savedRouteAvailable = false }

    /// Called at launch and whenever the app becomes active.
    func appBecameActive() {
        if DemoFlags.cubeConnected { return }  // DEBUG screenshots: no Bluetooth or location prompts
        cubeLink.connect()
        location.begin()
    }

    // MARK: planning

    /// One style's route for the preview, from the server (not MapKit); Calm follows the clock at `date`.
    func plan(to dest: Coord, style: RouteStyle, at date: Date = Date()) async -> Result<Route, StartError> {
        guard let server = ServerConfig.makeServer() else { return .failure(.badKey) }
        guard let from = location.fix?.coord else { return .failure(.noSignal) }
        do { return .success(try await server.route(from: from, to: dest, profile: style.profile(rushHour: isRushHour(date)))) }
        catch let e as StartError { return .failure(e) }
        catch { return .failure(.noSignal) }
    }

    static func text(for e: StartError) -> String {
        switch e {
        case .noSignal: return "No connection to the nav server. Check your mobile data or Wi-Fi."
        case .badKey: return "The server did not accept the key. Enter it in Settings."
        case .server(let c): return c == 0 ? "The nav server sent an answer the app cannot read." : "The nav server is not working right now (error \(c))."
        }
    }

    // MARK: ride

    /// Starts a ride on the picked preview route: downloads its map, then switches to the ride
    /// screen; reroutes ask for the same style. Background location goes on at once, so the download
    /// carries on with the phone pocketed. `cancelStart` (leaving the preview) abandons it.
    func start(to dest: Coord, option: RouteOption) {
        guard phase == .idle else { return }
        guard let server = ServerConfig.makeServer() else { message = Self.text(for: .badKey); return }
        let token = beginStarting()
        let e = RideEngine(server: server, cache: cache)
        e.frameBudget = cubeLink.frameBudget
        startTask = Task {
            var failure: StartError?
            do { try await e.start(route: option.route, to: dest, style: option.style) }
            catch let err as StartError { failure = err }
            catch { failure = .noSignal }
            guard token == startToken, phase == .starting else { return }   // cancelled
            startTask = nil
            guard let failure else { ride(e); return }
            endStarting()
            message = Self.text(for: failure)
            if failure == .noSignal { await offerSavedRoute(to: dest) }
        }
    }

    /// Restarts the cached ride to (about) this destination, offline.
    func startSaved(to dest: Coord) {
        guard phase == .idle else { return }
        guard let server = ServerConfig.makeServer() else { message = Self.text(for: .badKey); return }
        let token = beginStarting()
        let e = RideEngine(server: server, cache: cache)   // the server is only for rerouting
        e.frameBudget = cubeLink.frameBudget
        startTask = Task {
            let ok = await e.resumeLast(near: dest)
            guard token == startToken, phase == .starting else { return }
            startTask = nil
            if ok { ride(e); return }
            endStarting()
            message = "The saved route is gone. Start needs a connection."
        }
    }

    /// Abandons a Start still downloading; the ride screen does not come up.
    func cancelStart() {
        guard phase == .starting else { return }
        startToken += 1
        startTask?.cancel(); startTask = nil
        endStarting()
    }

    /// Shows "Start saved route (offline)" when the cached ride goes to within ~100 m of `dest`.
    func offerSavedRoute(to dest: Coord) async {
        let ok = await cache.hasRide(near: dest)
        if phase == .idle { savedRouteAvailable = ok }
    }

    private func beginStarting() -> Int {
        location.requestAlways()
        location.startRide()
        phase = .starting; message = nil; savedRouteAvailable = false
        startToken += 1
        return startToken
    }

    private func endStarting() {
        location.endRide()
        phase = .idle
    }

    private func ride(_ e: RideEngine) {
        engine = e
        phase = .riding
        if let f = location.fix { handle(f) }
        let t = DispatchSource.makeTimerSource(queue: .main)   // not a run-loop Timer: keeps firing in the background
        t.schedule(deadline: .now() + Self.minFrameGap, repeating: Self.minFrameGap)
        t.setEventHandler { [weak self] in MainActor.assumeIsolated { self?.tick() } }
        timer = t
        t.resume()
    }

    /// Always sends an `.ended` scene first; `CubeLink` keeps it for a cube that is not connected now.
    func stop() {
        guard let e = engine else { return }
        let ended = e.stop()
        cubeLink.send(ended)
        timer?.cancel(); timer = nil
        location.endRide()
        engine = nil; latest = nil; dirty = false; scene = nil; phase = .idle
    }

    private func handle(_ f: Fix) {
        guard phase == .riding, let e = engine else { return }
        lastUpdate = Date()
        apply(e.update(f))
    }

    /// Once a second without a new fix, lets the engine move on with time alone (arrival hold,
    /// reroute retries, weak GPS once the last fix is old). The last fix is not tracked again.
    private func tick() {
        if phase == .riding, let e = engine, Date().timeIntervalSince(lastUpdate) >= 1 {
            lastUpdate = Date()
            if let s = e.tick() { apply(s) }
        }
        flush()
    }

    private func apply(_ s: NavScene) {
        latest = s; scene = s; dirty = true
        if s.state == .ended { stop(); return }   // arrival hold over
        flush()
    }

    private func flush() {
        guard phase == .riding, let s = latest else { return }
        let age = Date().timeIntervalSince(lastSent)
        guard (dirty && age >= Self.minFrameGap) || age >= Self.maxFrameGap else { return }
        cubeLink.send(s)
        lastSent = Date(); dirty = false
    }
}

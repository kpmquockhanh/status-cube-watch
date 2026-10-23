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

    static let minFrameGap: TimeInterval = 0.2
    static let maxFrameGap: TimeInterval = 1.0

    private var engine: RideEngine?
    private var latest: NavScene?
    private var dirty = false
    private var lastSent = Date.distantPast
    private var timer: Timer?
    private var bag = Set<AnyCancellable>()
    private let cache = RideCache(directory: FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("ride"))

    private init() {
        cubeLink.$frameBudget.sink { [weak self] b in self?.engine?.frameBudget = b }.store(in: &bag)
        // Forward child changes so views that observe only the controller redraw.
        cubeLink.objectWillChange.sink { [weak self] in self?.objectWillChange.send() }.store(in: &bag)
        location.objectWillChange.sink { [weak self] in self?.objectWillChange.send() }.store(in: &bag)
        location.onFix = { [weak self] f in self?.handle(f) }
    }

    /// Called at launch and whenever the app becomes active.
    func appBecameActive() {
        cubeLink.connect()
        location.begin()
    }

    // MARK: planning

    /// The route for the preview map, from the server (not MapKit).
    func plan(to dest: Coord) async -> Result<Route, StartError> {
        guard let server = ServerConfig.makeServer() else { return .failure(.badKey) }
        guard let from = location.fix?.coord else { return .failure(.noSignal) }
        do { return .success(try await server.route(from: from, to: dest)) }
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

    func start(to dest: Coord) async {
        guard phase == .idle else { return }
        guard let server = ServerConfig.makeServer() else { message = Self.text(for: .badKey); return }
        guard let from = location.fix?.coord else { message = "Waiting for a GPS fix. Allow location access and try again."; return }
        location.requestAlways()
        phase = .starting; message = nil
        let e = RideEngine(server: server, cache: cache)
        e.frameBudget = cubeLink.frameBudget
        do { try await e.start(from: from, to: dest) }
        catch let err as StartError { phase = .idle; message = Self.text(for: err); return }
        catch { phase = .idle; message = Self.text(for: .noSignal); return }
        engine = e
        location.startRide()
        phase = .riding
        if let f = location.fix { handle(f) }
        timer = Timer.scheduledTimer(withTimeInterval: Self.minFrameGap, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.tick() }
        }
    }

    /// Always sends an `.ended` scene first; `CubeLink` keeps it for a cube that is not connected now.
    func stop() {
        guard let e = engine else { phase = .idle; return }
        let ended = e.stop()
        cubeLink.send(ended)
        timer?.invalidate(); timer = nil
        location.endRide()
        engine = nil; latest = nil; dirty = false; scene = nil; phase = .idle
    }

    private func handle(_ f: Fix) {
        guard phase == .riding, let e = engine else { return }
        let s = e.update(f)
        latest = s; scene = s; dirty = true
        if s.state == .ended { stop(); return }   // arrival hold over
        flush()
    }

    private func tick() { flush() }

    private func flush() {
        guard phase == .riding, let s = latest else { return }
        let age = Date().timeIntervalSince(lastSent)
        guard (dirty && age >= Self.minFrameGap) || age >= Self.maxFrameGap else { return }
        cubeLink.send(s)
        lastSent = Date(); dirty = false
    }
}

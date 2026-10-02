import Foundation

public enum SupervisorAction: Equatable {
    case adopt                // something already serves the port: use it, do not spawn
    case spawn(node: String)
    case fail(String)
}

/// Runs `node server.mjs` as a child of the (Bluetooth-permitted) app and keeps
/// it alive. If a bridge from agent.sh is already running it is adopted instead.
public final class BridgeSupervisor {
    public static func decide(externalBridgeUp: Bool, node: String?) -> SupervisorAction {
        if externalBridgeUp { return .adopt }
        guard let node else { return .fail("node not found: install Node 20+ or set CUBE_NODE to its path") }
        return .spawn(node: node)
    }

    public static func findNode(env: [String: String], exists: (String) -> Bool) -> String? {
        if let p = env["CUBE_NODE"], exists(p) { return p }
        for p in ["/opt/homebrew/bin/node", "/usr/local/bin/node", "/usr/bin/node"] where exists(p) { return p }
        return nil
    }

    /// A child that stayed up this long counts as healthy, so the next crash restarts quickly again.
    public static func shouldResetBackoff(uptime: TimeInterval) -> Bool { uptime >= 60 }

    private var spawnedAt: Date?
    private let bridgeDir: URL
    private let port: Int
    private let node: String?
    private let log: (String) -> Void
    private let client: BridgeClient
    private var process: Process?
    private var stopping = false
    private var backoff = Backoff()

    public init(bridgeDir: URL, port: Int, node: String?, log: @escaping (String) -> Void) {
        self.bridgeDir = bridgeDir
        self.port = port
        self.node = node
        self.log = log
        self.client = BridgeClient(port: port)
    }

    /// Call on the main queue.
    public func start() {
        stopping = false
        client.healthy { [weak self] up in
            DispatchQueue.main.async { self?.act(externalUp: up) }
        }
    }

    public func stop() {
        stopping = true
        Trace.log("bridge", "stop requested")
        process?.terminate()
    }

    /// Restarts a bridge this app spawned (it comes back after the backoff); otherwise re-checks the port.
    public func restart() {
        Trace.log("bridge", "restart requested (child: \(process != nil))")
        if let process { process.terminate() } else { start() }
    }

    private func act(externalUp: Bool) {
        guard !stopping else { return }
        Trace.log("bridge", "health check on :\(port): \(externalUp ? "up" : "down"), node \(node ?? "none"), spawned child: \(process != nil)")
        switch Self.decide(externalBridgeUp: externalUp, node: node) {
        case .adopt:
            if process == nil {
                log("bridge already running on :\(port); using it")
                recheck(after: 30)  // if it goes away, take over
            }
        case .fail(let why):
            log(why)
            recheck(after: 30)
        case .spawn(let node):
            if process == nil { spawn(node) }
        }
    }

    private func recheck(after seconds: TimeInterval) {
        DispatchQueue.main.asyncAfter(deadline: .now() + seconds) { [weak self] in self?.start() }
    }

    private func spawn(_ node: String) {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: node)
        p.arguments = ["server.mjs"]
        p.currentDirectoryURL = bridgeDir
        var env = ProcessInfo.processInfo.environment
        env["CUBE_PORT"] = String(port)
        p.environment = env
        Trace.log("bridge", "spawn \(node) server.mjs cwd \(bridgeDir.path) CUBE_PORT=\(port)")
        p.terminationHandler = { [weak self] proc in
            DispatchQueue.main.async { self?.exited(status: proc.terminationStatus) }
        }
        do {
            try p.run()
            process = p
            spawnedAt = Date()
            log("started bridge (pid \(p.processIdentifier)) in \(bridgeDir.path)")
        } catch {
            log("could not start bridge: \(error.localizedDescription)")
            recheck(after: backoff.next())
        }
    }

    private func exited(status: Int32) {
        Trace.log("bridge", "child exited, status \(status), uptime \(spawnedAt.map { Int(Date().timeIntervalSince($0)) } ?? -1) s")
        process = nil
        if let t = spawnedAt, Self.shouldResetBackoff(uptime: Date().timeIntervalSince(t)) { backoff.reset() }
        spawnedAt = nil
        guard !stopping else { return }
        let delay = backoff.next()
        log("bridge exited (status \(status)); restarting in \(Int(delay)) s")
        recheck(after: delay)
    }
}

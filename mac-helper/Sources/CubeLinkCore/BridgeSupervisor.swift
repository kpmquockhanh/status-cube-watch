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
        process?.terminate()
    }

    private func act(externalUp: Bool) {
        guard !stopping else { return }
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
        p.terminationHandler = { [weak self] proc in
            DispatchQueue.main.async { self?.exited(status: proc.terminationStatus) }
        }
        do {
            try p.run()
            process = p
            log("started bridge (pid \(p.processIdentifier)) in \(bridgeDir.path)")
        } catch {
            log("could not start bridge: \(error.localizedDescription)")
            recheck(after: backoff.next())
        }
    }

    private func exited(status: Int32) {
        process = nil
        guard !stopping else { return }
        let delay = backoff.next()
        log("bridge exited (status \(status)); restarting in \(Int(delay)) s")
        recheck(after: delay)
    }
}

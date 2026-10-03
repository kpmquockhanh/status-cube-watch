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

    /// `recorded` is the path install.sh stored in the app's defaults (`nodePath`), for launches
    /// that do not carry the LaunchAgent's environment (Finder, `open`) and nvm-style installs.
    public static func findNode(env: [String: String], recorded: String? = nil, exists: (String) -> Bool) -> String? {
        if let p = env["CUBE_NODE"], exists(p) { return p }
        if let p = recorded, exists(p) { return p }
        for p in ["/opt/homebrew/bin/node", "/usr/local/bin/node", "/usr/bin/node"] where exists(p) { return p }
        return nil
    }

    /// The bridge directory: the first of CUBE_BRIDGE_DIR, the one install.sh stored (`bridgeDir`), and
    /// a `bridge/` in `searchFrom` (the working directory, the app bundle) or up to 4 of its parents,
    /// that holds server.mjs. Nil when none does.
    public static func findBridgeDir(env: [String: String], recorded: String?, searchFrom: [String],
                                     exists: (String) -> Bool) -> String? {
        var candidates = [env["CUBE_BRIDGE_DIR"], recorded].compactMap { $0 }.filter { !$0.isEmpty }
        for start in searchFrom where !start.isEmpty {
            var dir = start
            for _ in 0..<5 {
                candidates.append((dir as NSString).appendingPathComponent("bridge"))
                let up = (dir as NSString).deletingLastPathComponent
                if up.isEmpty || up == dir { break }
                dir = up
            }
        }
        return candidates.first { exists(($0 as NSString).appendingPathComponent("server.mjs")) }
    }

    /// A child that stayed up this long counts as healthy, so the next crash restarts quickly again.
    public static func shouldResetBackoff(uptime: TimeInterval) -> Bool { uptime >= 60 }

    /// The line to log for a decision, or nil when it repeats the previous one: the port is
    /// re-checked every 30 s while adopted or failing, and that must not fill the log.
    public static func announcement(_ action: SupervisorAction, previous: SupervisorAction?, port: Int) -> String? {
        guard action != previous else { return nil }
        switch action {
        case .adopt: return "bridge already running on :\(port); using it"
        case .fail(let why): return why
        case .spawn: return nil  // spawn() logs the pid
        }
    }

    private var spawnedAt: Date?
    private let bridgeDir: URL
    private let port: Int
    private let node: String?
    private let log: (String) -> Void
    private let client: BridgeClient
    private var process: Process?
    private var stopping = false
    private var backoff = Backoff()
    private var lastAction: SupervisorAction?
    private var pendingRecheck: DispatchWorkItem?

    /// `client` is shared with the app's own polling; nil makes one for `port`.
    public init(bridgeDir: URL, port: Int, node: String?, client: BridgeClient? = nil, log: @escaping (String) -> Void) {
        self.bridgeDir = bridgeDir
        self.port = port
        self.node = node
        self.log = log
        self.client = client ?? BridgeClient(port: port)
    }

    /// Call on the main queue.
    public func start() {
        stopping = false
        client.healthy { [weak self] up in
            DispatchQueue.main.async { self?.act(externalUp: up) }
        }
    }

    /// Call on the main queue. Ends the child (SIGTERM), so quitting never leaves an orphaned bridge
    /// that the next launch would adopt.
    public func stop() {
        stopping = true
        Trace.log("bridge", "stop requested")
        pendingRecheck?.cancel()
        pendingRecheck = nil
        process?.terminate()
    }

    /// Restarts a bridge this app spawned (it comes back after the backoff); otherwise re-checks the port.
    public func restart() {
        Trace.log("bridge", "restart requested (child: \(process != nil))")
        if let process {
            process.terminate()
        } else {
            lastAction = nil  // log the outcome again, so the click visibly did something
            start()
        }
    }

    private func act(externalUp: Bool) {
        guard !stopping else { return }
        Trace.log("bridge", "health check on :\(port): \(externalUp ? "up" : "down"), node \(node ?? "none"), spawned child: \(process != nil)")
        let action = Self.decide(externalBridgeUp: externalUp, node: node)
        switch action {
        case .adopt where process != nil:
            return  // our own child answered; exited() takes over if it dies
        case .adopt, .fail:
            if let line = Self.announcement(action, previous: lastAction, port: port) { log(line) }
            recheck(after: 30)  // adopted: if it goes away, take over; failed: try again
        case .spawn(let node):
            if process == nil { spawn(node) }
        }
        lastAction = action
    }

    /// One pending re-check at most: a new one replaces it, so restarts never stack up loops.
    private func recheck(after seconds: TimeInterval) {
        pendingRecheck?.cancel()
        let item = DispatchWorkItem { [weak self] in
            self?.pendingRecheck = nil
            self?.start()
        }
        pendingRecheck = item
        DispatchQueue.main.asyncAfter(deadline: .now() + seconds, execute: item)
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

import AppKit
import CubeLinkCore
import Foundation

setvbuf(stdout, nil, _IOLBF, 0)

func log(_ s: String) {
    let f = ISO8601DateFormatter()
    FileHandle.standardError.write(Data("\(f.string(from: Date())) \(s)\n".utf8))
}

// One instance only: a second launch (e.g. `open` after the LaunchAgent started one) exits quietly.
func acquireSingleInstanceLock() -> Bool {
    let dir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("ClaudeCubeLink")
    try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    let fd = open(dir.appendingPathComponent("lock").path, O_CREAT | O_RDWR, 0o644)
    if fd < 0 {
        log("cannot open lock file: \(String(cString: strerror(errno)))")
        exit(1)
    }
    return flock(fd, LOCK_EX | LOCK_NB) == 0  // the fd stays open for the life of the process
}

guard acquireSingleInstanceLock() else {
    log("already running; exiting")
    exit(0)
}

let env = ProcessInfo.processInfo.environment
var port = Int(env["CUBE_PORT"] ?? "") ?? 8787
let argv = CommandLine.arguments
if let i = argv.firstIndex(of: "--port"), i + 1 < argv.count, let p = Int(argv[i + 1]) { port = p }

let bridgeDir = URL(fileURLWithPath: env["CUBE_BRIDGE_DIR"] ?? FileManager.default.currentDirectoryPath + "/bridge")
let node = BridgeSupervisor.findNode(env: env, exists: { FileManager.default.isExecutableFile(atPath: $0) })

let supervisor = BridgeSupervisor(bridgeDir: bridgeDir, port: port, node: node, log: log)
let client = BridgeClient(port: port)
let link = CubeLink(log: log)
var policy = PushPolicy(heartbeat: 5)
var lastBridgeError = ""
var lastBody: Data?
var bridgeUp = false

let app = NSApplication.shared
app.setActivationPolicy(.accessory)
let statusMenu = StatusMenu()

func refreshMenu() {
    statusMenu.apply(MenuModel.make(payload: lastBody, bridgeUp: bridgeUp, link: link.state))
}

func tick(force: Bool) {
    client.fetch { body, why in
        DispatchQueue.main.async {
            if body == nil, let why, why != lastBridgeError {
                lastBridgeError = why
                log("bridge: \(why)")
            }
            if body != nil { lastBridgeError = "" }
            bridgeUp = body != nil
            if let body { lastBody = body }
            refreshMenu()
            guard link.isReady else { return }
            let now = Date()
            if let body, policy.shouldSend(body: body, now: now, force: force) {
                link.send(body)
                policy.didSend(body: body, at: now)
            }
        }
    }
}

link.onSendNow = { tick(force: true) }
link.onStateChange = { _ in refreshMenu() }
statusMenu.onSendNow = { tick(force: true) }
statusMenu.onRestartBridge = { supervisor.restart() }
supervisor.start()
Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { _ in tick(force: false) }
log("Claude Cube Link started (bridge :\(port), dir \(bridgeDir.path))")

var signalSources: [DispatchSourceSignal] = []
for sig in [SIGTERM, SIGINT] {
    signal(sig, SIG_IGN)
    let src = DispatchSource.makeSignalSource(signal: sig, queue: .main)
    src.setEventHandler {
        supervisor.stop()
        exit(0)
    }
    src.resume()
    signalSources.append(src)
}
app.run()

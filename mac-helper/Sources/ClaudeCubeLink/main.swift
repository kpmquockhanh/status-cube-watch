import AppKit
import CubeLinkCore
import Foundation

setvbuf(stdout, nil, _IOLBF, 0)

// Same file install.sh points the LaunchAgent's stderr at. When launched any other way
// (Finder, `open`), stderr is not a file yet, so send it there too: the menu's "Show log" always has something to open.
let logURL = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/claude-cube-link.log")
var stderrStat = stat()
if fstat(STDERR_FILENO, &stderrStat) != 0 || (stderrStat.st_mode & S_IFMT) != S_IFREG {
    try? FileManager.default.createDirectory(at: logURL.deletingLastPathComponent(), withIntermediateDirectories: true)
    freopen(logURL.path, "a", stderr)
}

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
Trace.configure(env: env, args: CommandLine.arguments)
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
            Trace.log("tick", "force \(force), body \(body?.count ?? 0) B, link ready \(link.isReady), link state \(link.state)")
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
            if let body {
                let send = policy.shouldSend(body: body, now: now, force: force)
                Trace.log("tick", "push policy: \(send ? "send" : "skip")")
                if send {
                    link.send(body)
                    policy.didSend(body: body, at: now)
                }
            }
        }
    }
}

let settingsWindow = SettingsWindow()
settingsWindow.onApply = { link.writeSettings($0) }
link.onSettings = { settingsWindow.render($0) }
link.onSettingsResult = { settingsWindow.showResult($0) }
statusMenu.onShowSettings = {
    link.refreshSettings()  // the cube may have been edited on its own screen since
    settingsWindow.show()
}

link.onSendNow = { tick(force: true) }
let notifier = Notifier()
notifier.onAuthorizationChange = { statusMenu.authorization = $0 }
statusMenu.onMenuWillOpen = { notifier.refreshAuthorization(askIfUnanswered: true) }
link.onPomodoroEnded = { ended, next in
    guard statusMenu.prefs.enabled else {
        Trace.log("main", "pomodoro notice muted")
        return
    }
    notifier.post(PomodoroNotice.make(ended: ended, next: next))
}
notifier.start()
link.onStateChange = { Trace.log("link", "state -> \($0)"); refreshMenu() }
statusMenu.onSendNow = { tick(force: true) }
statusMenu.onRestartBridge = { supervisor.restart() }
statusMenu.logURL = logURL
supervisor.start()
Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { _ in tick(force: false) }
log("Claude Cube Link started (bridge :\(port), dir \(bridgeDir.path))")
Trace.log("main", "tracing on; node \(node ?? "not found")")

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

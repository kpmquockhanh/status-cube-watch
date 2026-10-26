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

func log(_ s: String) { Trace.info(s) }  // one writer for log and trace lines, so they stay in order

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

// After the environment, what install.sh recorded in the app's defaults: a launch from Finder or
// `open` has no LaunchAgent environment and "/" as its working directory. The bridge normally ships
// inside the bundle (Contents/Resources/bridge); a bridge/ near the working directory or the app
// bundle covers `swift run` in the repo.
let cwd = FileManager.default.currentDirectoryPath
let bridgeDir = URL(fileURLWithPath: BridgeSupervisor.findBridgeDir(
    env: env, recorded: UserDefaults.standard.string(forKey: "bridgeDir"),
    searchFrom: [Bundle.main.resourcePath ?? "", cwd, Bundle.main.bundlePath],
    exists: { FileManager.default.isExecutableFile(atPath: $0) })
    ?? env["CUBE_BRIDGE_DIR"] ?? cwd + "/bridge")
let binaryPath = bridgeDir.appendingPathComponent("cube-bridge").path
let binaryFound = FileManager.default.isExecutableFile(atPath: binaryPath)

// The bridge's own settings (source, extra cards, mail, LAN): kept in the app, handed to the child as env.
let bridgeStore = BridgeSettingsStore(log: log)
bridgeStore.importConfigIfOffered()

let client = BridgeClient(port: port)
let supervisor = BridgeSupervisor(bridgeDir: bridgeDir, port: port, client: client,
                                  childEnvironment: { bridgeStore.settings.environment(inherited: $0, secrets: bridgeStore.secrets) },
                                  log: log)
let link = CubeLink(log: log)
var policy = PushPolicy(heartbeat: 5)
var lastBridgeError = ""
var lastBody: Data?
var bridgeUp = false
var lastFetch: Date?
var fetching = false
var answerPolicy = AnswerPolicy()
/// The agent whose detail screen is open on the cube (Control 0A); cleared when the link leaves ready.
var detailAgent: String?

/// An accessory app shows no menu bar, but key equivalents still go through the main menu: without an
/// Edit menu, Cmd-C/V/X/A/Z do nothing in the settings window's text fields.
func makeMainMenu() -> NSMenu {
    let appMenu = NSMenu()
    appMenu.addItem(withTitle: "Close Window", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
    let edit = NSMenu(title: "Edit")
    edit.addItem(withTitle: "Undo", action: Selector(("undo:")), keyEquivalent: "z")
    let redo = edit.addItem(withTitle: "Redo", action: Selector(("redo:")), keyEquivalent: "z")
    redo.keyEquivalentModifierMask = [.command, .shift]
    edit.addItem(.separator())
    edit.addItem(withTitle: "Cut", action: #selector(NSText.cut(_:)), keyEquivalent: "x")
    edit.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
    edit.addItem(withTitle: "Paste", action: #selector(NSText.paste(_:)), keyEquivalent: "v")
    edit.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
    let bar = NSMenu()
    for sub in [appMenu, edit] {
        let i = NSMenuItem()
        i.submenu = sub
        bar.addItem(i)
    }
    return bar
}

/// Every way out that goes through NSApp.terminate (the menu's Quit, logout, shutdown) ends here.
final class AppDelegate: NSObject, NSApplicationDelegate {
    var onTerminate: () -> Void = {}
    func applicationDidFinishLaunching(_ notification: Notification) { NSApp.mainMenu = makeMainMenu() }
    func applicationWillTerminate(_ notification: Notification) { onTerminate() }
}

let app = NSApplication.shared
app.setActivationPolicy(.accessory)
let appDelegate = AppDelegate()  // NSApp.delegate is weak; this global keeps it alive
// Stop the bridge child, or it outlives the app and the next launch adopts it, stale code and config included.
appDelegate.onTerminate = {
    nowPlaying.stop()
    supervisor.stop()
}
app.delegate = appDelegate
let statusMenu = StatusMenu()

func refreshMenu() {
    statusMenu.apply(MenuModel.make(payload: lastBody, bridgeUp: bridgeUp, link: link.state))
}

/// Runs every second; fetches `/api/status` at `PollPolicy`'s pace (a little early, so 5 s stays 5 s),
/// at once when forced, and never while a fetch is still out.
func tick(force: Bool) {
    let now = Date()
    if !force {
        if fetching { return }
        if let last = lastFetch, now.timeIntervalSince(last) < PollPolicy.interval(payload: lastBody, detailOpen: detailAgent != nil) - 0.25 { return }
    }
    lastFetch = now
    fetching = true
    client.fetch(detail: detailAgent) { body, why in
        DispatchQueue.main.async {
            fetching = false
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

let bridgeSettingsWindow = BridgeSettingsWindow()
/// What the window says about who runs the bridge, before and after a save.
func bridgeSettingsNote(saved: Bool) -> String {
    if supervisor.isAdopted {
        return (saved ? "Saved, but the" : "The") + " bridge on :\(port) was started outside this app and keeps "
            + "its own config. Stop it (bridge/agent.sh uninstall) and these apply."
    }
    return saved ? "Saved. Restarting the bridge…" : "Apply saves and restarts the bridge."
}
bridgeSettingsWindow.onApply = { s, secrets in
    if let why = bridgeStore.save(s, secrets) { return "Not saved to the Keychain: \(why)" }
    let note = bridgeSettingsNote(saved: true)
    if !supervisor.isAdopted { supervisor.restart() }
    return note
}
statusMenu.onShowBridgeSettings = {
    bridgeSettingsWindow.show(bridgeStore.settings, bridgeStore.secrets, note: bridgeSettingsNote(saved: false))
}

link.onSendNow = { tick(force: true) }
let systemVolume = SystemVolume()
let nowPlaying = NowPlaying(log: log)
let screenLock = ScreenLock()
let unlocker = Unlocker(log: log)
/// Locked as of now. The `ScreenLock` flag alone is not enough: it changes only when the
/// `com.apple.screenIsLocked` notification is handled on main, so a tap handled just before that is
/// still "unlocked", and with fast user switching (session off the console) it may never be set.
/// An answer types into a session, so ask the session itself at the moment it matters.
func macLockedNow() -> Bool {
    screenLock.isLocked || sessionLocked(CGSessionCopyCurrentDictionary() as? [String: Any])
}
var unlockPolicy = UnlockPolicy()
/// The Volume frame: the output's state, whether anything is playing, and whether the cube should
/// offer to unlock the Mac.
func volumeFrame(_ v: MacVolume) -> Data {
    var v = v
    v.playing = nowPlaying.playing
    v.locked = UnlockPolicy.offer(enabled: unlocker.isEnabled, locked: screenLock.isLocked)
    v.screenLocked = macLockedNow()
    return encodeVolume(v)
}
systemVolume.onChange = { v in
    Trace.log("volume", "mac \(v.level.map(String.init) ?? "none")\(v.muted ? " muted" : "") \(v.name)")
    _ = link.writeVolume(volumeFrame(v))
}
nowPlaying.onChange = { playing in
    Trace.log("media", "now playing: \(playing.map { $0 ? "playing" : "paused" } ?? "unknown")")
    _ = link.writeVolume(volumeFrame(systemVolume.current()))
}
link.onReady = { _ = link.writeVolume(volumeFrame(systemVolume.current())) }
link.onVolumeRequest = { level, muted in
    Trace.log("volume", "cube asks \(level)\(muted ? " muted" : "")")
    systemVolume.apply(level: level, muted: muted)
}
systemVolume.start()
let mediaKeys = MediaKeys(log: log)
nowPlaying.start()
link.onMediaKey = { key in
    Trace.log("media", "cube presses \(key)")
    mediaKeys.press(key)
}
screenLock.onChange = { locked in
    Trace.log("unlock", "screen \(locked ? "locked" : "unlocked")")
    _ = link.writeVolume(volumeFrame(systemVolume.current()))
}
link.onUnlock = {
    switch unlockPolicy.decide(enabled: unlocker.isEnabled, locked: screenLock.isLocked,
                               trusted: unlocker.isTrusted, now: Date()) {
    case .type:
        log("unlocking: tapped on the cube")
        unlocker.unlock(saverStopped: RunningSaver.stop())
    case .ignore(let why):
        log("unlock request ignored: \(why)")
    }
}
link.onDetail = { agent in
    guard agent != detailAgent else { return }
    detailAgent = agent
    Trace.log("detail", "cube shows \(agent ?? "no detail")")
    tick(force: true)
}
link.onHat = { agent, hat in
    client.setHat(agent: agent, hat: hat) { status, error in
        DispatchQueue.main.async {
            log("hat for \(agent): \(hatOutcome(status: status, error: error))")
            tick(force: true)
        }
    }
}
link.onAnswer = { agent, prompt, key in
    switch answerPolicy.decide(locked: macLockedNow()) {
    case .ignore(let why):
        log("answer for \(agent) ignored: \(why)")
    case .post:
        client.answer(agent: agent, prompt: prompt, key: key) { status, error in
            DispatchQueue.main.async {
                answerPolicy.finished()
                log("answer \(key) for \(agent)/\(prompt): \(describe(answerOutcome(status: status, error: error)))")
                // The cube learns from the next payload; the bridge rebuilds it ~150 ms after the
                // keys land, so fetch a little later or this would still show the old prompt.
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { tick(force: true) }
            }
        }
    }
}
statusMenu.unlockOn = unlocker.isEnabled
statusMenu.onToggleUnlock = {
    if unlocker.isEnabled { unlocker.disable() } else { unlocker.enableInteractively() }
    statusMenu.unlockOn = unlocker.isEnabled
    _ = link.writeVolume(volumeFrame(systemVolume.current()))
}
screenLock.start()
link.onStateChange = {
    Trace.log("link", "state -> \($0)")
    if !link.isReady { detailAgent = nil }  // a detail screen does not outlive its link
    refreshMenu()
}
statusMenu.onSendNow = { tick(force: true) }
statusMenu.onRestartBridge = { supervisor.restart() }
statusMenu.onForgetCube = { link.forgetCube() }
statusMenu.onOpenBoard = { NSWorkspace.shared.open(URL(string: "http://127.0.0.1:\(port)/board/")!) }
statusMenu.logURL = logURL
supervisor.start()
// In .common modes, not just .default: the heartbeat must keep going while the status menu is open,
// or after 15 s the cube calls the link stale and falls back to WiFi. Ticks every second; tick()
// decides whether this one fetches (1 s while an agent waits, else 5 s).
let tickTimer = Timer(timeInterval: 1, repeats: true) { _ in tick(force: false) }
RunLoop.main.add(tickTimer, forMode: .common)

/// Waits on the bridge's `GET /api/changes` in a loop and fetches as soon as the agents change, so a
/// new status reaches the cube within a moment rather than at the next 5 s poll (`ChangesWatch`).
func watchChanges() {
    client.waitForChange { status in
        DispatchQueue.main.async {
            let next = ChangesWatch.after(status: status)
            Trace.log("changes", "status \(status ?? -1), fetch \(next.fetch)")
            if next.fetch { tick(force: true) }
            DispatchQueue.main.asyncAfter(deadline: .now() + next.delay) { watchChanges() }
        }
    }
}
watchChanges()
log("Claude Cube Link started (bridge :\(port), dir \(bridgeDir.path))")
Trace.log("main", "tracing on; bridge binary \(binaryFound ? binaryPath : "not found")")

var signalSources: [DispatchSourceSignal] = []
for sig in [SIGTERM, SIGINT] {
    signal(sig, SIG_IGN)
    let src = DispatchSource.makeSignalSource(signal: sig, queue: .main)
    src.setEventHandler {
        nowPlaying.stop()
        supervisor.stop()
        exit(0)
    }
    src.resume()
    signalSources.append(src)
}
app.run()

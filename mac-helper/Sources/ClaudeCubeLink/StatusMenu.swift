import AppKit
import CubeLinkCore

/// The menu bar item: icon + 5h percent, with a dropdown of the cards, status and actions.
final class StatusMenu: NSObject {
    var onSendNow: () -> Void = {}
    var onRestartBridge: () -> Void = {}
    var onShowSettings: () -> Void = {}
    var onShowBridgeSettings: () -> Void = {}
    var onForgetCube: () -> Void = {}
    var onToggleUnlock: () -> Void = {}
    var onOpenBoard: () -> Void = {}
    /// The "Unlock Mac with cube…" checkbox: a password is stored.
    var unlockOn = false {
        didSet { unlockItem?.state = unlockOn ? .on : .off }
    }
    private var unlockItem: NSMenuItem?
    var logURL: URL?
    private var applied: MenuModel?

    private let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let menu = NSMenu()

    override init() {
        super.init()
        item.menu = menu
        item.button?.image = NSImage(systemSymbolName: "cube", accessibilityDescription: "Claude Cube")
        item.button?.imagePosition = .imageLeading
        apply(MenuModel.make(payload: nil, bridgeUp: false, link: .searching))
    }

    func apply(_ m: MenuModel) {
        guard m != applied else { return }  // called every 5 s; rebuild only when something changed
        applied = m
        if let b = item.button {
            let color: NSColor? = m.tint == .ink ? nil : tintColor(m.tint)  // ink: the menu bar's own text colour
            var attrs: [NSAttributedString.Key: Any] = [.font: NSFont.monospacedDigitSystemFont(ofSize: 12, weight: .medium)]
            if let color { attrs[.foregroundColor] = color }
            let title = NSMutableAttributedString(string: " " + m.barTitle, attributes: attrs)
            if m.agents?.waiting == true {  // an agent needs you: the accent dot the cube's alert would beep for
                title.append(NSAttributedString(string: " ●", attributes: [.font: NSFont.systemFont(ofSize: 9),
                                                                          .foregroundColor: tintColor(.orange),
                                                                          .baselineOffset: 1]))
            }
            b.attributedTitle = title
            b.appearsDisabled = !m.cubeConnected
        }
        menu.removeAllItems()
        if m.cards.isEmpty { menu.addItem(view(StatusRowView("No data yet", dot: .systemGray))) }
        for c in m.cards {
            menu.addItem(view(CompactUsageRowView.fits(c) ? CompactUsageRowView(c) : UsageRowView(c)))
        }
        if let a = m.agents {
            menu.addItem(.separator())
            menu.addItem(view(HeadingRowView("Agents", detail: a.caption.lowercased())))
            if !a.shown.isEmpty {
                let tiles = AgentTilesView(a.shown, width: menuRowWidth)
                tiles.onTile = { [weak self] _ in self?.onOpenBoard() }
                menu.addItem(view(tiles))
            }
            let more = a.hidden > 0 ? "Open board (\(a.hidden) more)" : "Open board in browser"
            menu.addItem(action(more, #selector(openBoard), key: "b"))
        }
        menu.addItem(.separator())
        menu.addItem(view(StatusRowView(m.bridgeLine, dot: m.bridgeUp ? .systemGreen : .systemRed)))
        let cubeDot: NSColor = switch m.link {
        case .connected: .systemGreen
        case .searching, .connecting: .systemYellow  // waiting, not an alarm: orange is the deck's alarm colour
        case .bluetoothOff, .unauthorized: .systemRed
        }
        menu.addItem(view(StatusRowView(m.cubeLine, dot: cubeDot)))
        menu.addItem(.separator())
        menu.addItem(action("Send now", #selector(sendNow), key: "s"))
        menu.addItem(action("Cube settings…", #selector(showSettings), key: ","))
        menu.addItem(action("Bridge settings…", #selector(showBridgeSettings), key: ""))
        let unlock = action("Unlock Mac with cube…", #selector(toggleUnlock), key: "")
        unlock.state = unlockOn ? .on : .off
        unlockItem = unlock
        menu.addItem(unlock)
        menu.addItem(.separator())
        // Rarely needed, and Forget cube cannot be undone: kept a step away.
        let trouble = NSMenu()
        let troubleItem = NSMenuItem(title: "Troubleshoot", action: nil, keyEquivalent: "")
        trouble.addItem(action("Restart bridge", #selector(restartBridge), key: "r"))
        trouble.addItem(action("Show log", #selector(showLog), key: "l"))
        let trace = action("Verbose trace", #selector(toggleTrace), key: "")
        trace.state = Trace.enabled ? .on : .off
        trouble.addItem(trace)
        trouble.addItem(.separator())
        trouble.addItem(action("Forget cube", #selector(forgetCube), key: ""))
        troubleItem.submenu = trouble
        menu.addItem(troubleItem)
        menu.addItem(.separator())
        menu.addItem(action("Quit Claude Cube Link", #selector(quit), key: "q"))
    }

    private func view(_ v: NSView) -> NSMenuItem {
        let i = NSMenuItem()
        i.view = v
        return i
    }

    private func action(_ title: String, _ sel: Selector, key: String) -> NSMenuItem {
        let i = NSMenuItem(title: title, action: sel, keyEquivalent: key)
        i.target = self
        return i
    }

    @objc private func sendNow() { onSendNow() }
    @objc private func restartBridge() { onRestartBridge() }
    @objc private func showSettings() { onShowSettings() }
    @objc private func showBridgeSettings() { onShowBridgeSettings() }
    @objc private func forgetCube() { onForgetCube() }
    @objc private func toggleUnlock() { onToggleUnlock() }
    @objc private func openBoard() { onOpenBoard() }
    @objc private func showLog() {
        guard let url = logURL else { return }
        if !FileManager.default.fileExists(atPath: url.path) {
            FileManager.default.createFile(atPath: url.path, contents: nil)
        }
        // Console.app is the usual .log handler; fall back to revealing the file.
        if !NSWorkspace.shared.open(url) { NSWorkspace.shared.activateFileViewerSelecting([url]) }
    }

    @objc private func toggleTrace(_ sender: NSMenuItem) {
        Trace.enabled.toggle()
        sender.state = Trace.enabled ? .on : .off
        Trace.log("main", "tracing on (from menu)")
    }

    // applicationWillTerminate (main.swift) stops the bridge child on the way out.
    @objc private func quit() { NSApp.terminate(nil) }
}

import AppKit
import CubeLinkCore

/// The menu bar item: icon + 5h percent, with a dropdown of the cards, status and actions.
final class StatusMenu: NSObject {
    var onSendNow: () -> Void = {}
    var onRestartBridge: () -> Void = {}
    var logURL: URL?

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
        if let b = item.button {
            let color: NSColor? = switch m.tint {
            case .normal: nil
            case .amber: .systemOrange
            case .red: .systemRed
            }
            var attrs: [NSAttributedString.Key: Any] = [.font: NSFont.monospacedDigitSystemFont(ofSize: 12, weight: .medium)]
            if let color { attrs[.foregroundColor] = color }
            b.attributedTitle = NSAttributedString(string: " " + m.barTitle, attributes: attrs)
            b.appearsDisabled = !m.cubeConnected
        }
        menu.removeAllItems()
        menu.addItem(view(sectionHeader("Usage")))
        if m.cards.isEmpty { menu.addItem(view(StatusRowView("No data yet", dot: .systemGray))) }
        for c in m.cards { menu.addItem(view(UsageRowView(c))) }
        menu.addItem(.separator())
        menu.addItem(view(StatusRowView(m.bridgeLine, dot: m.bridgeUp ? .systemGreen : .systemRed)))
        let cubeDot: NSColor = switch m.link {
        case .connected: .systemGreen
        case .searching, .connecting: .systemOrange
        case .bluetoothOff, .unauthorized: .systemRed
        }
        menu.addItem(view(StatusRowView(m.cubeLine, dot: cubeDot)))
        menu.addItem(.separator())
        menu.addItem(action("Send now", #selector(sendNow), key: "s"))
        menu.addItem(action("Restart bridge", #selector(restartBridge), key: "r"))
        menu.addItem(.separator())
        menu.addItem(action("Show log", #selector(showLog), key: "l"))
        let trace = action("Verbose trace", #selector(toggleTrace), key: "")
        trace.state = Trace.enabled ? .on : .off
        menu.addItem(trace)
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

    @objc private func quit() { NSApp.terminate(nil) }
}

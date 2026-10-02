import AppKit
import CubeLinkCore

/// The menu bar item: icon + 5h percent, with a dropdown of the cards, status and actions.
final class StatusMenu: NSObject {
    var onSendNow: () -> Void = {}
    var onRestartBridge: () -> Void = {}

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
        for c in m.cards {
            menu.addItem(label(c.title, bold: true))
            if !c.detail.isEmpty { menu.addItem(label(c.detail)) }
        }
        if !m.cards.isEmpty { menu.addItem(.separator()) }
        menu.addItem(label(m.bridgeLine))
        menu.addItem(label(m.cubeLine))
        menu.addItem(.separator())
        menu.addItem(action("Send now", #selector(sendNow), key: "s"))
        menu.addItem(action("Restart bridge", #selector(restartBridge), key: "r"))
        menu.addItem(.separator())
        menu.addItem(action("Quit Claude Cube Link", #selector(quit), key: "q"))
    }

    private func label(_ s: String, bold: Bool = false) -> NSMenuItem {
        let i = NSMenuItem()
        i.attributedTitle = NSAttributedString(string: s, attributes: [
            .font: bold ? NSFont.menuFont(ofSize: 0).withWeight(.semibold) : NSFont.menuFont(ofSize: 0),
            .foregroundColor: bold ? NSColor.labelColor : NSColor.secondaryLabelColor,
        ])
        return i
    }

    private func action(_ title: String, _ sel: Selector, key: String) -> NSMenuItem {
        let i = NSMenuItem(title: title, action: sel, keyEquivalent: key)
        i.target = self
        return i
    }

    @objc private func sendNow() { onSendNow() }
    @objc private func restartBridge() { onRestartBridge() }
    @objc private func quit() { NSApp.terminate(nil) }
}

private extension NSFont {
    func withWeight(_ w: NSFont.Weight) -> NSFont {
        NSFont.systemFont(ofSize: pointSize, weight: w)
    }
}

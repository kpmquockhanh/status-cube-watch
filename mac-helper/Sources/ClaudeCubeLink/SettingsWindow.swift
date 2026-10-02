import AppKit
import CubeLinkCore

/// "Cube settings…": edits what the cube's setup portal edits, over Bluetooth.
/// Fields start from what the cube reported; Apply sends only what changed.
final class SettingsWindow: NSObject, NSWindowDelegate {
    /// Returns false when the cube cannot take a write right now.
    var onApply: (Data) -> Bool = { _ in false }

    private let window: NSWindow
    private var current: CubeSettings?

    private let backlight = NSSlider(value: 160, minValue: 10, maxValue: 255, target: nil, action: nil)
    private let sleep = SettingsWindow.number(CubeSettings.sleepRange)
    private let rotate = SettingsWindow.number(CubeSettings.rotateRange)
    private let poll = SettingsWindow.number(CubeSettings.pollRange)
    private let focus = SettingsWindow.number(CubeSettings.minutesRange)
    private let short = SettingsWindow.number(CubeSettings.minutesRange)
    private let long = SettingsWindow.number(CubeSettings.minutesRange)
    private let sessions = SettingsWindow.number(CubeSettings.sessionsRange)
    private let ssid = NSTextField()
    private let wifiPass = NSSecureTextField()
    private let bridge = NSTextField()
    private let otaPass = NSSecureTextField()
    private let status = NSTextField(labelWithString: "")
    private let apply = NSButton(title: "Apply", target: nil, action: nil)

    override init() {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 440, height: 520),
                          styleMask: [.titled, .closable], backing: .buffered, defer: true)
        super.init()
        window.title = "Cube settings"
        window.isReleasedWhenClosed = false
        window.delegate = self

        let grid = NSGridView(views: [
            [header("Display")],
            [label("Brightness"), backlight],
            [label("Screen sleep (min, 0 = never)"), sleep],
            [label("Auto-advance cards (s, 0 = off)"), rotate],
            [label("WiFi refresh (s)"), poll],
            [header("Pomodoro")],
            [label("Focus (min)"), focus],
            [label("Short break (min)"), short],
            [label("Long break (min)"), long],
            [label("Sessions before long break"), sessions],
            [header("Network (the cube reboots when these change)")],
            [label("WiFi network"), ssid],
            [label("WiFi password"), wifiPass],
            [label("Bridge URL"), bridge],
            [label("OTA password"), otaPass],
        ])
        grid.rowSpacing = 8
        grid.columnSpacing = 12
        grid.column(at: 0).xPlacement = .trailing
        for r in [0, 5, 10] {
            grid.row(at: r).mergeCells(in: NSRange(location: 0, length: 2))
            grid.row(at: r).topPadding = r == 0 ? 0 : 10
        }
        for f in [ssid, wifiPass, bridge, otaPass] { f.widthAnchor.constraint(equalToConstant: 220).isActive = true }
        backlight.widthAnchor.constraint(equalToConstant: 220).isActive = true
        wifiPass.placeholderString = "leave blank to keep"
        otaPass.placeholderString = "leave blank to keep"
        bridge.placeholderString = "http://192.168.1.50:8787/api/status"

        apply.target = self
        apply.action = #selector(applyTapped)
        apply.keyEquivalent = "\r"
        status.textColor = .secondaryLabelColor
        status.lineBreakMode = .byWordWrapping
        status.maximumNumberOfLines = 2
        status.preferredMaxLayoutWidth = 280
        let bottom = NSStackView(views: [status, apply])
        bottom.distribution = .fill
        status.setContentHuggingPriority(.defaultLow, for: .horizontal)

        let stack = NSStackView(views: [grid, bottom])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 16
        stack.edgeInsets = NSEdgeInsets(top: 20, left: 20, bottom: 20, right: 20)
        bottom.widthAnchor.constraint(equalTo: grid.widthAnchor).isActive = true
        window.contentView = stack
        window.setContentSize(stack.fittingSize)
        render(nil)
    }

    func show() {
        NSApp.activate(ignoringOtherApps: true)
        window.center()
        window.makeKeyAndOrderFront(nil)
    }

    /// The cube reported its settings (nil: link down, or firmware without the Settings characteristic).
    func render(_ s: CubeSettings?) {
        current = s
        let on = s != nil
        for c in controls { c.isEnabled = on }
        apply.isEnabled = on
        guard let s else {
            status.stringValue = "Not connected to a cube that supports Bluetooth settings."
            return
        }
        // After our own write the cube re-reports, so the fields show what it actually stored.
        backlight.integerValue = s.backlight
        sleep.integerValue = s.sleepMin
        rotate.integerValue = s.rotateSec
        poll.integerValue = s.pollSec
        focus.integerValue = s.focusMin
        short.integerValue = s.shortMin
        long.integerValue = s.longMin
        sessions.integerValue = s.sessions
        ssid.stringValue = s.ssid
        bridge.stringValue = s.bridge
        wifiPass.stringValue = ""
        otaPass.stringValue = ""
        wifiPass.placeholderString = s.wifiPassSet ? "set; leave blank to keep" : "none (open network)"
        otaPass.placeholderString = s.otaPassSet ? "set; leave blank to keep" : "none"
        status.stringValue = "Connected."
    }

    func showResult(_ r: SettingsResult) {
        switch r {
        case .ok: status.stringValue = "Saved on the cube."
        case .okReboot: status.stringValue = "Saved. The cube is rebooting and will reconnect."
        case .invalid: status.stringValue = "The cube rejected those values."
        }
    }

    // MARK: private

    private var controls: [NSControl] {
        [backlight, sleep, rotate, poll, focus, short, long, sessions, ssid, wifiPass, bridge, otaPass]
    }

    @objc private func applyTapped() {
        guard let old = current else { return }
        var new = old
        new.backlight = backlight.integerValue
        new.sleepMin = sleep.integerValue
        new.rotateSec = rotate.integerValue
        new.pollSec = poll.integerValue
        new.focusMin = focus.integerValue
        new.shortMin = short.integerValue
        new.longMin = long.integerValue
        new.sessions = sessions.integerValue
        new.ssid = ssid.stringValue
        new.bridge = bridge.stringValue.trimmingCharacters(in: .whitespaces)
        guard new.isValid else {
            status.stringValue = "A value is out of range."
            return
        }
        let newWifi = wifiPass.stringValue.isEmpty ? nil : wifiPass.stringValue
        let newOta = otaPass.stringValue.isEmpty ? nil : otaPass.stringValue
        guard let patch = new.patch(from: old, wifiPass: newWifi, otaPass: newOta) else {
            status.stringValue = "Nothing changed."
            return
        }
        status.stringValue = onApply(patch) ? "Sending…" : "The cube is not reachable right now."
    }

    private static func number(_ range: ClosedRange<Int>) -> NSTextField {
        let f = NSTextField()
        let nf = NumberFormatter()
        nf.numberStyle = .none
        nf.minimum = NSNumber(value: range.lowerBound)
        nf.maximum = NSNumber(value: range.upperBound)
        nf.allowsFloats = false
        f.formatter = nf
        f.alignment = .right
        f.widthAnchor.constraint(equalToConstant: 70).isActive = true
        return f
    }

    private func label(_ s: String) -> NSTextField { NSTextField(labelWithString: s) }

    private func header(_ s: String) -> NSTextField {
        let l = NSTextField(labelWithString: s)
        l.font = .boldSystemFont(ofSize: 12)
        return l
    }
}

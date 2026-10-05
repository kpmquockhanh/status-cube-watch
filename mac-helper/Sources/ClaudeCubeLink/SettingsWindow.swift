import AppKit
import CubeLinkCore

/// "Cube settings…": edits what the cube's setup portal edits, over Bluetooth.
/// Fields start from what the cube reported; Apply sends only what changed.
final class SettingsWindow: NSObject, NSWindowDelegate {
    /// Returns false when the cube cannot take a write right now.
    var onApply: (Data) -> Bool = { _ in false }

    private let window: NSWindow
    /// What the cube reports now (nil while disconnected): Apply diffs against it.
    private var current: CubeSettings?
    /// What the fields were last filled from; kept across a disconnect so edits survive a reconnect.
    private var shown: CubeSettings?
    /// The outcome of the last write. Re-renders keep it, since the cube re-reports right after.
    private var note: String?
    private var rebooting = false
    /// Set by a successful save: the next report replaces every field, so they show what was stored.
    private var refill = false

    private let backlight = NSSlider(value: 160, minValue: 10, maxValue: 255, target: nil, action: nil)
    private let sleep = SettingsWindow.number(CubeSettings.sleepRange)
    private let rotate = SettingsWindow.number(CubeSettings.rotateRange)
    private let poll = SettingsWindow.number(CubeSettings.pollRange)
    private let sound = NSPopUpButton(frame: .zero, pullsDown: false)
    /// Hidden until a cube reports `sd` (fw_rev 4): an older one would ignore it.
    private var soundRow: NSGridRow?
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
            [label("Sound"), sound],
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
        for r in [0, 6, 11] {
            grid.row(at: r).mergeCells(in: NSRange(location: 0, length: 2))
            grid.row(at: r).topPadding = r == 0 ? 0 : 10
        }
        sound.addItems(withTitles: ["Off", "Low", "Medium", "High"])  // index = level
        soundRow = grid.row(at: 5)
        soundRow?.isHidden = true
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
        if !window.isVisible, let s = current {
            // Reopening starts from the cube's values: edits from a closed window are dropped.
            fill(s)
            shown = s
            wifiPass.stringValue = ""
            otaPass.stringValue = ""
            note = nil
            status.stringValue = "Connected."
        }
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
            status.stringValue = rebooting ? (note ?? "") : "Not connected to a cube that supports Bluetooth settings."
            return
        }
        if rebooting {  // back after the reboot a write caused
            rebooting = false
            note = nil
        }
        // After a save every field shows what the cube actually stored; otherwise untouched fields
        // follow the cube and fields being edited keep what the user typed.
        let base: CubeSettings? = refill ? nil : shown
        fill(base.map { CubeSettings.rebase(form: form(over: $0), shown: $0, onto: s) } ?? s)
        shown = s
        refill = false
        wifiPass.placeholderString = s.wifiPassSet ? "set; leave blank to keep" : "none (open network)"
        otaPass.placeholderString = s.otaPassSet ? "set; leave blank to keep" : "none"
        status.stringValue = note ?? "Connected."
    }

    func showResult(_ r: SettingsResult) {
        switch r {
        case .ok: note = "Saved on the cube."
        case .okReboot: note = "Saved. The cube is rebooting and will reconnect."
        case .invalid: note = "The cube rejected those values."  // the fields keep them, to fix and retry
        }
        rebooting = r == .okReboot
        refill = r != .invalid
        if refill {
            wifiPass.stringValue = ""
            otaPass.stringValue = ""
        }
        status.stringValue = note ?? ""
    }

    // MARK: private

    private var controls: [NSControl] {
        [backlight, sleep, rotate, poll, sound, focus, short, long, sessions, ssid, wifiPass, bridge, otaPass]
    }

    /// `base` with the field values on top (the passwords are not part of CubeSettings).
    private func form(over base: CubeSettings) -> CubeSettings {
        var s = base
        s.backlight = backlight.integerValue
        s.sleepMin = sleep.integerValue
        s.rotateSec = rotate.integerValue
        s.pollSec = poll.integerValue
        if base.sound != nil { s.sound = sound.indexOfSelectedItem }
        s.focusMin = focus.integerValue
        s.shortMin = short.integerValue
        s.longMin = long.integerValue
        s.sessions = sessions.integerValue
        s.ssid = ssid.stringValue
        s.bridge = bridge.stringValue.trimmingCharacters(in: .whitespaces)
        return s
    }

    /// Writes only the fields that differ: assigning to the field being typed in would move its caret.
    private func fill(_ s: CubeSettings) {
        func setInt(_ c: NSControl, _ v: Int) { if c.integerValue != v { c.integerValue = v } }
        func setText(_ f: NSTextField, _ v: String) { if f.stringValue != v { f.stringValue = v } }
        setInt(backlight, s.backlight)
        setInt(sleep, s.sleepMin)
        setInt(rotate, s.rotateSec)
        setInt(poll, s.pollSec)
        if let v = s.sound, sound.indexOfSelectedItem != v { sound.selectItem(at: v) }
        let hide = s.sound == nil
        if let row = soundRow, row.isHidden != hide {
            row.isHidden = hide
            if let v = window.contentView { window.setContentSize(v.fittingSize) }
        }
        setInt(focus, s.focusMin)
        setInt(short, s.shortMin)
        setInt(long, s.longMin)
        setInt(sessions, s.sessions)
        setText(ssid, s.ssid)
        setText(bridge, s.bridge)
    }

    @objc private func applyTapped() {
        guard let old = current else { return }
        let new = form(over: old)
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
        // The cube treats a new network without a password as an open one and erases the stored password.
        if new.joinsOpenNetwork(from: old, wifiPass: newWifi), !confirmOpenNetwork(new.ssid) {
            status.stringValue = "Not sent. Enter the WiFi password for the new network."
            return
        }
        note = nil
        rebooting = false
        guard onApply(patch) else {
            status.stringValue = "The cube is not reachable right now."
            return
        }
        status.stringValue = CubeSettings.needsReboot(patch) ? "Sending… The cube reboots to apply network changes." : "Sending…"
    }

    private func confirmOpenNetwork(_ ssid: String) -> Bool {
        let a = NSAlert()
        a.messageText = "Join “\(ssid)” as an open network?"
        a.informativeText = "No WiFi password was entered, so the cube joins this network without one and "
            + "forgets the stored password. Enter the password first if the network has one."
        a.addButton(withTitle: "Cancel")
        a.addButton(withTitle: "Join Open Network")
        return a.runModal() == .alertSecondButtonReturn
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

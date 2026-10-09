import AppKit
import CubeLinkCore

/// "Bridge settings…": what bridge/config.json used to hold, saved in the app. Apply saves and
/// restarts the bridge child so it starts with the new environment.
final class BridgeSettingsWindow: NSObject, NSWindowDelegate {
    /// Saves; returns the status line to show.
    var onApply: (BridgeSettings, BridgeSecrets) -> String = { _, _ in "" }

    private let window: NSWindow
    private let extraCards = NSButton(checkboxWithTitle: "Show spend and token cards", target: nil, action: nil)
    private let source = NSPopUpButton(frame: .zero, pullsDown: false)
    private let adminKey = NSSecureTextField()
    private let oauthToken = NSSecureTextField()
    private let refresh = NSTextField()
    private let gmailUser = NSTextField()
    private let gmailPassword = NSSecureTextField()
    private let lan = NSButton(checkboxWithTitle: "Serve on the network (for cubes that poll over WiFi)", target: nil, action: nil)
    private let status = NSTextField(labelWithString: "")
    private let apply = NSButton(title: "Apply", target: nil, action: nil)

    override init() {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 440, height: 400),
                          styleMask: [.titled, .closable], backing: .buffered, defer: true)
        super.init()
        window.title = "Bridge settings"
        window.isReleasedWhenClosed = false
        window.delegate = self

        let grid = NSGridView(views: [
            [header("Usage")],
            [NSGridCell.emptyContentView, extraCards],
            [label("Source"), source],
            [label("Admin API key"), adminKey],
            [label("Admin OAuth token"), oauthToken],
            [label("Refresh (s)"), refresh],
            [header("Mail")],
            [label("Gmail address"), gmailUser],
            [label("App password"), gmailPassword],
            [header("Network")],
            [NSGridCell.emptyContentView, lan],
        ])
        grid.rowSpacing = 8
        grid.columnSpacing = 12
        grid.column(at: 0).xPlacement = .trailing
        for r in [0, 6, 9] {
            grid.row(at: r).mergeCells(in: NSRange(location: 0, length: 2))
            grid.row(at: r).topPadding = r == 0 ? 0 : 10
        }
        source.addItems(withTitles: ["Local Claude Code logs", "Admin API (organisation)"])  // order = Source.allCases
        for f in [adminKey, oauthToken, gmailUser, gmailPassword] {
            f.widthAnchor.constraint(equalToConstant: 240).isActive = true
        }
        adminKey.placeholderString = "sk-ant-admin…"
        oauthToken.placeholderString = "optional, instead of the key"
        gmailUser.placeholderString = "off when blank"
        gmailPassword.placeholderString = "Google app password"
        let nf = NumberFormatter()
        nf.numberStyle = .none
        nf.allowsFloats = false
        nf.minimum = NSNumber(value: BridgeSettings.refreshRange.lowerBound)
        nf.maximum = NSNumber(value: BridgeSettings.refreshRange.upperBound)
        refresh.formatter = nf
        refresh.alignment = .right
        refresh.placeholderString = "default"
        refresh.widthAnchor.constraint(equalToConstant: 70).isActive = true
        source.target = self
        source.action = #selector(styleUsage)

        apply.target = self
        apply.action = #selector(applyTapped)
        apply.keyEquivalent = "\r"
        status.textColor = .secondaryLabelColor
        status.lineBreakMode = .byWordWrapping
        status.maximumNumberOfLines = 3
        status.preferredMaxLayoutWidth = 300
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
    }

    /// Opens the window. Reopening starts from what is saved: edits in a closed window are dropped.
    func show(_ s: BridgeSettings, _ secrets: BridgeSecrets, note: String) {
        if !window.isVisible {
            extraCards.state = s.extraCards ? .on : .off
            source.selectItem(at: BridgeSettings.Source.allCases.firstIndex(of: s.source) ?? 0)
            adminKey.stringValue = secrets.adminKey
            oauthToken.stringValue = secrets.oauthToken
            refresh.stringValue = s.refreshSec.map(String.init) ?? ""
            gmailUser.stringValue = s.gmailUser
            gmailPassword.stringValue = secrets.gmailPassword
            lan.state = s.lan ? .on : .off
            status.stringValue = note
            styleUsage()
        }
        NSApp.activate(ignoringOtherApps: true)
        window.center()
        window.makeKeyAndOrderFront(nil)
    }

    /// The admin fields only matter for the admin source. Disabled fields still keep and save their values.
    @objc private func styleUsage() {
        let admin = BridgeSettings.Source.allCases[source.indexOfSelectedItem] == .admin
        for f in [adminKey, oauthToken] { f.isEnabled = admin }
    }

    @objc private func applyTapped() {
        window.makeFirstResponder(nil)  // commit the field being typed in, through its formatter
        var s = BridgeSettings()
        s.extraCards = extraCards.state == .on
        s.source = BridgeSettings.Source.allCases[source.indexOfSelectedItem]
        let r = refresh.stringValue.trimmingCharacters(in: .whitespaces)
        s.refreshSec = r.isEmpty ? nil : Int(r)
        if !r.isEmpty, s.refreshSec == nil {
            status.stringValue = "Refresh must be a whole number of seconds."
            return
        }
        s.gmailUser = gmailUser.stringValue.trimmingCharacters(in: .whitespaces)
        s.lan = lan.state == .on
        let secrets = BridgeSecrets(adminKey: adminKey.stringValue.trimmingCharacters(in: .whitespaces),
                                    oauthToken: oauthToken.stringValue.trimmingCharacters(in: .whitespaces),
                                    gmailPassword: gmailPassword.stringValue)
        if let why = s.problem(secrets: secrets) {
            status.stringValue = why
            return
        }
        status.stringValue = onApply(s, secrets)
    }

    private func label(_ s: String) -> NSTextField { NSTextField(labelWithString: s) }

    private func header(_ s: String) -> NSTextField {
        let l = NSTextField(labelWithString: s)
        l.font = .boldSystemFont(ofSize: 12)
        return l
    }
}

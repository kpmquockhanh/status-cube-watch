import SwiftUI

/// The cube link in plain words. Connected (or still looking) is a quiet pill; anything the user
/// has to act on becomes a card with the fix and the buttons.
struct CubeStatusView: View {
    @ObservedObject var link: CubeLink
    private var status: CubeLink.Status { DemoFlags.cubeConnected ? .connected : link.status }
    /// Where a pill is shown elsewhere on the screen, show only the card (nothing when fine).
    var cardOnly = false

    var body: some View {
        if needsAction {
            card
        } else if !cardOnly {
            StatusPill(text: headline, color: color)
        }
    }

    private var card: some View {
        Panel {
            VStack(alignment: .leading, spacing: 10) {
                Label {
                    Text(headline).font(.headline).foregroundStyle(Theme.ink)
                } icon: {
                    Image(systemName: icon).foregroundStyle(color)
                }
                if let d = detail { Text(d).font(.subheadline).foregroundStyle(Theme.dim) }
                HStack(spacing: 10) {
                    if needsSettings {
                        Button("Open Settings") {
                            if let u = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(u) }
                        }
                        .buttonStyle(SecondaryButtonStyle())
                    }
                    if status != .connected && status != .searching {
                        Button("Try again") { link.connect() }
                            .buttonStyle(SecondaryButtonStyle(tint: Theme.accent))
                    }
                }
            }
        }
    }

    private var needsAction: Bool { status != .connected && status != .searching }

    private var needsSettings: Bool { status == .unauthorized || status == .forgetCube || status == .off }

    private var headline: String {
        switch status {
        case .off: return "Bluetooth is off"
        case .unauthorized: return "Bluetooth is not allowed"
        case .searching: return "Looking for the cube"
        case .pairing: return "Pairing with the cube"
        case .openPairingWindow: return "Cube is not accepting this phone"
        case .connected: return "Cube connected"
        case .tooOld: return "Cube firmware too old"
        case .forgetCube: return "Cube pairing is out of date"
        }
    }

    private var detail: String? {
        switch status {
        case .off: return "Turn Bluetooth on (Control Center or Settings), then try again."
        case .unauthorized: return "Allow CubeNav under Settings > Privacy & Security > Bluetooth."
        case .searching: return "Keep the cube powered and close to the phone."
        case .pairing: return "Enter the code shown on the cube when the iPhone asks for it."
        case .openPairingWindow: return "Open Pair new device in the cube's setup portal, then try again."
        case .connected: return nil
        case .tooOld: return "Update the cube's firmware (it needs a version with navigation, revision 8 or newer), then try again."
        case .forgetCube: return "Forget Claude Cube in iOS Settings > Bluetooth, then try again."
        }
    }

    private var icon: String {
        switch status {
        case .connected: return "checkmark.circle.fill"
        case .searching, .pairing: return "antenna.radiowaves.left.and.right"
        default: return "exclamationmark.triangle.fill"
        }
    }

    private var color: Color {
        switch status {
        case .connected: return Theme.good
        case .searching, .pairing: return Theme.route
        default: return Theme.warn
        }
    }
}

/// The cube link as a pill only, whatever its state (screens that show the card elsewhere).
struct CubeStatusPillOnly: View {
    @ObservedObject var link: CubeLink
    var body: some View {
        let status = DemoFlags.cubeConnected ? .connected : link.status
        let ok = status == .connected
        let busy = status == .searching || status == .pairing
        StatusPill(text: ok ? "Cube connected" : busy ? "Looking for the cube" : "Cube not linked",
                   color: ok ? Theme.good : busy ? Theme.route : Theme.warn)
    }
}

/// Warns when the 7-day free-account profile is about to lapse.
struct ExpiryBanner: View {
    var body: some View {
        if let exp = ServerConfig.expiry() {
            let left = exp.timeIntervalSinceNow
            if left < 2 * 86400 {
                let days = max(0, Int(ceil(left / 86400)))
                Label(left <= 0 ? "Rebuild the app from Xcode: it has stopped working"
                                : "Rebuild the app from Xcode: it stops working in \(days) day\(days == 1 ? "" : "s")",
                      systemImage: "hammer.fill")
                    .font(.subheadline.weight(.semibold))
                    .foregroundStyle(Color.black)
                    .padding(12).frame(maxWidth: .infinity, alignment: .leading)
                    .background(left < 12 * 3600 ? Theme.bad : Theme.warn,
                                in: RoundedRectangle(cornerRadius: 16, style: .continuous))
            }
        }
    }
}

/// DEBUG-only switches for simulator screenshots (the simulator has no Bluetooth).
enum DemoFlags {
    #if DEBUG
    static let cubeConnected = ProcessInfo.processInfo.arguments.contains("-demoCubeConnected")
    #else
    static let cubeConnected = false
    #endif
}

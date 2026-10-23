import SwiftUI

/// The cube link in plain words, with the fix where there is one.
struct CubeStatusView: View {
    @ObservedObject var link: CubeLink

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Label(headline, systemImage: icon).font(.headline).foregroundStyle(color)
            if let d = detail { Text(d).font(.subheadline).foregroundStyle(.secondary) }
            HStack {
                if needsSettings {
                    Button("Open Settings") {
                        if let u = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(u) }
                    }
                }
                if link.status != .connected && link.status != .searching {
                    Button("Try again") { link.connect() }
                }
            }.buttonStyle(.bordered)
        }
    }

    private var needsSettings: Bool { link.status == .unauthorized || link.status == .forgetCube || link.status == .off }

    private var headline: String {
        switch link.status {
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
        switch link.status {
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
        switch link.status {
        case .connected: return "checkmark.circle.fill"
        case .searching, .pairing: return "antenna.radiowaves.left.and.right"
        default: return "exclamationmark.triangle.fill"
        }
    }

    private var color: Color {
        switch link.status {
        case .connected: return .green
        case .searching, .pairing: return .primary
        default: return .orange
        }
    }
}

/// Warns when the 7-day free-account profile is about to lapse.
struct ExpiryBanner: View {
    var body: some View {
        if let exp = ServerConfig.expiry() {
            let left = exp.timeIntervalSinceNow
            if left < 2 * 86400 {
                let days = max(0, Int(ceil(left / 86400)))
                Text(left <= 0 ? "Rebuild the app from Xcode: it has stopped working"
                               : "Rebuild the app from Xcode: it stops working in \(days) day\(days == 1 ? "" : "s")")
                    .font(.subheadline.bold())
                    .foregroundStyle(.white)
                    .padding(10).frame(maxWidth: .infinity, alignment: .leading)
                    .background(left < 12 * 3600 ? Color.red : Color.orange, in: RoundedRectangle(cornerRadius: 8))
            }
        }
    }
}

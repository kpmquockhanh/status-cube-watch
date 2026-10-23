import SwiftUI
import NavCore

struct RideView: View {
    @EnvironmentObject var ride: RideController

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            CubeStatusView(link: ride.cubeLink)
            Divider()
            if let s = ride.scene {
                Text(stateText(s)).font(.title2.bold()).foregroundStyle(s.state == .navigating ? Color.primary : .orange)
                if !s.dist.isEmpty { Text(s.dist).font(.system(size: 56, weight: .bold, design: .rounded)) }
                if !s.street.isEmpty { Text(s.street).font(.title3) }
                if !s.eta.isEmpty { Text("Arrive \(s.eta)").foregroundStyle(.secondary) }
            } else {
                Text("Waiting for GPS…").font(.title2)
            }
            Label(signal(ride.scene), systemImage: "location.fill").font(.subheadline).foregroundStyle(.secondary)
            Spacer()
            Button(role: .destructive) { ride.stop() } label: { Text("Stop").frame(maxWidth: .infinity) }
                .buttonStyle(.borderedProminent).controlSize(.large).tint(.red)
        }
        .padding()
    }

    private func stateText(_ s: NavScene) -> String {
        switch s.state {
        case .navigating: return "Navigating"
        case .rerouting: return "Rerouting…"
        case .offRouteNoSignal: return "Off route, no signal"
        case .arrived: return "You have arrived"
        case .ended: return "Ended"
        }
    }

    private func signal(_ s: NavScene?) -> String {
        guard let f = ride.location.fix, let at = ride.location.fixAt, Date().timeIntervalSince(at) < 5 else { return "No GPS signal" }
        if s?.gpsWeak == true || f.accuracy > 50 { return "GPS weak (±\(Int(f.accuracy)) m)" }
        return "GPS good (±\(Int(f.accuracy)) m)"
    }
}

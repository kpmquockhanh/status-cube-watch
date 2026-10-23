import SwiftUI
import NavCore

struct RideView: View {
    @EnvironmentObject var ride: RideController

    var body: some View {
        RideScreen(scene: ride.scene, link: ride.cubeLink, location: ride.location, onStop: { ride.stop() })
    }
}

/// The ride, split from `RideController` so it can be shown with a sample scene (DEBUG demo).
struct RideScreen: View {
    let scene: NavScene?
    @ObservedObject var link: CubeLink
    @ObservedObject var location: LocationService
    let onStop: () -> Void
    @State private var confirmStop = false

    var body: some View {
        ZStack {
            Theme.bg.ignoresSafeArea()
            ScrollView {
                VStack(spacing: 14) {
                    HStack(spacing: 8) {
                        CubeStatusPillOnly(link: link)
                        Spacer()
                        StatusPill(text: gpsText, color: gpsColor, systemImage: "location.fill")
                    }
                    CubeStatusView(link: link, cardOnly: true)
                    warnings
                    turnCard
                    mirror
                }
                .padding(.horizontal, Theme.gutter)
                .padding(.top, 8)
                .padding(.bottom, 96)
            }
            VStack {
                Spacer()
                Button(role: .destructive) { confirmStop = true } label: {
                    Label("End ride", systemImage: "stop.fill")
                }
                .buttonStyle(SecondaryButtonStyle(tint: Theme.bad))
                .padding(.horizontal, Theme.gutter)
                .padding(.bottom, 8)
                .background(LinearGradient(colors: [Theme.bg.opacity(0), Theme.bg], startPoint: .top, endPoint: .center)
                    .ignoresSafeArea())
            }
        }
        .confirmationDialog("End this ride?", isPresented: $confirmStop, titleVisibility: .visible) {
            Button("End ride", role: .destructive, action: onStop)
        } message: {
            Text("The cube goes back to its cards.")
        }
    }

    // MARK: turn card

    @ViewBuilder private var turnCard: some View {
        if let s = scene {
            Panel(padding: 20) {
                VStack(alignment: .leading, spacing: 14) {
                    HStack(alignment: .center, spacing: 16) {
                        TurnGlyph(turn: s.turn, exit: s.exit, dir: s.dir)
                            .font(.system(size: 38, weight: .bold))
                            .foregroundStyle(Color.black)
                            .frame(width: 76, height: 76)
                            .background(stateColor(s), in: RoundedRectangle(cornerRadius: 22, style: .continuous))
                        VStack(alignment: .leading, spacing: 2) {
                            if s.state == .arrived {
                                Text("Arrived").font(.system(size: 40, weight: .bold, design: .rounded))
                            } else if s.state == .rerouting {
                                Text("Rerouting").font(.system(size: 34, weight: .bold, design: .rounded))
                            } else {
                                Text(s.dist).font(Theme.number(52)).minimumScaleFactor(0.6).lineLimit(1)
                            }
                            Text(stateText(s)).font(.subheadline.weight(.semibold)).foregroundStyle(stateColor(s))
                        }
                        .foregroundStyle(Theme.ink)
                    }
                    if !s.street.isEmpty {
                        Text(s.street).font(.title3.weight(.semibold)).foregroundStyle(Theme.ink).lineLimit(2)
                    }
                    if !s.eta.isEmpty {
                        HStack(spacing: 6) {
                            Image(systemName: "clock").foregroundStyle(Theme.dim)
                            Text("Arrive in \(s.eta)").foregroundStyle(Theme.dim)
                        }
                        .font(.subheadline)
                    }
                }
            }
            .accessibilityElement(children: .combine)
        } else {
            Panel(padding: 24) {
                HStack(spacing: 14) {
                    ProgressView().tint(Theme.accent)
                    Text("Waiting for GPS…").font(.title3.weight(.semibold)).foregroundStyle(Theme.ink)
                }
            }
        }
    }

    @ViewBuilder private var mirror: some View {
        if let s = scene {
            VStack(spacing: 10) {
                SectionLabel("On the cube")
                CubeMirror(scene: s)
                    .frame(maxWidth: 230)
                    .shadow(color: Theme.route.opacity(0.18), radius: 30)
            }
            .padding(.top, 6)
        }
    }

    // MARK: warnings

    @ViewBuilder private var warnings: some View {
        if let p = location.problem {
            notice(p, icon: "location.slash.fill", tint: Theme.bad)
        }
        if location.authorization != .authorizedAlways {
            Panel {
                VStack(alignment: .leading, spacing: 10) {
                    Label("Allow location Always so the cube keeps updating with the phone locked.",
                          systemImage: "lock.iphone")
                        .font(.subheadline).foregroundStyle(Theme.ink)
                    Button("Open Settings") {
                        if let u = URL(string: UIApplication.openSettingsURLString) { UIApplication.shared.open(u) }
                    }
                    .buttonStyle(SecondaryButtonStyle(tint: Theme.accent))
                }
            }
        }
    }

    private func notice(_ text: String, icon: String, tint: Color) -> some View {
        Label(text, systemImage: icon)
            .font(.subheadline).foregroundStyle(Theme.ink)
            .padding(12).frame(maxWidth: .infinity, alignment: .leading)
            .background(Theme.surface, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 16, style: .continuous).strokeBorder(tint.opacity(0.6)))
    }

    // MARK: words and colours

    private func stateText(_ s: NavScene) -> String {
        switch s.state {
        case .navigating: return TurnGlyph.words(s.turn).capitalizedFirst
        case .rerouting: return "Finding a new route"
        case .offRouteNoSignal: return "Off route · no signal"
        case .arrived: return "You have arrived"
        case .ended: return "Ride ended"
        }
    }

    private func stateColor(_ s: NavScene) -> Color {
        switch s.state {
        case .navigating: return Theme.accent
        case .rerouting: return Theme.route
        case .offRouteNoSignal: return Theme.warn
        case .arrived, .ended: return Theme.good
        }
    }

    private var gpsText: String {
        guard let f = location.fix, let at = location.fixAt, Date().timeIntervalSince(at) < 5 else { return "No GPS" }
        if scene?.gpsWeak == true || f.accuracy > 50 { return "GPS weak ±\(Int(f.accuracy)) m" }
        return "GPS ±\(Int(f.accuracy)) m"
    }

    private var gpsColor: Color {
        guard let f = location.fix, let at = location.fixAt, Date().timeIntervalSince(at) < 5 else { return Theme.bad }
        return scene?.gpsWeak == true || f.accuracy > 50 ? Theme.warn : Theme.good
    }
}

private extension String {
    var capitalizedFirst: String { prefix(1).uppercased() + dropFirst() }
}

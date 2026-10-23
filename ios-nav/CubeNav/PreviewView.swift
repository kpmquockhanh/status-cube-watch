import MapKit
import SwiftUI
import NavCore

struct PreviewView: View {
    @EnvironmentObject var ride: RideController
    @Environment(\.dismiss) private var dismiss
    let place: Place
    /// DEBUG demo only: a fixed route instead of asking the server.
    var demoRoute: Route? = nil
    @State private var route: Route?
    @State private var error: String?
    @State private var loading = true

    var body: some View {
        ZStack(alignment: .top) {
            Map {
                if let route {
                    let line = route.points.map { CLLocationCoordinate2D(latitude: $0.lat, longitude: $0.lon) }
                    MapPolyline(coordinates: line).stroke(Theme.bg, style: StrokeStyle(lineWidth: 11, lineCap: .round, lineJoin: .round))
                    MapPolyline(coordinates: line).stroke(Theme.route, style: StrokeStyle(lineWidth: 7, lineCap: .round, lineJoin: .round))
                }
                Marker(place.name, systemImage: "flag.checkered", coordinate: CLLocationCoordinate2D(latitude: place.lat, longitude: place.lon))
                    .tint(Theme.accent)
                UserAnnotation()
            }
            .mapStyle(.standard(elevation: .flat, emphasis: .muted, pointsOfInterest: .excludingAll))
            .mapControls { MapCompass() }
            .ignoresSafeArea()

            VStack(spacing: 12) {
                HStack {
                    RoundIconButton(systemName: "chevron.left", label: "Back") { dismiss() }
                    Spacer()
                    CubeStatusPillOnly(link: ride.cubeLink)
                }
                Spacer()
                panel
            }
            .padding(.horizontal, Theme.gutter)
            .padding(.bottom, 8)
        }
        .toolbar(.hidden, for: .navigationBar)
        .task { await load() }
        // Leaving the preview while the map downloads abandons the Start.
        .onDisappear { ride.cancelStart() }
    }

    private var panel: some View {
        Panel(padding: 18) {
            VStack(alignment: .leading, spacing: 16) {
                VStack(alignment: .leading, spacing: 3) {
                    Text(place.name).font(.title2.weight(.bold)).foregroundStyle(Theme.ink).lineLimit(2)
                    if !place.subtitle.isEmpty {
                        Text(place.subtitle).font(.subheadline).foregroundStyle(Theme.dim).lineLimit(1)
                    }
                }
                if loading {
                    HStack(spacing: 10) {
                        ProgressView().tint(Theme.accent)
                        Text("Finding a motorbike route…").font(.subheadline).foregroundStyle(Theme.dim)
                    }
                    .frame(minHeight: 58)
                } else if let route {
                    HStack(spacing: 0) {
                        stat(Self.km(route.distance), "Distance")
                        divider
                        stat(formatETA(route.time), "Ride time")
                        divider
                        stat(Date().addingTimeInterval(route.time).formatted(date: .omitted, time: .shortened), "Arrive")
                    }
                }
                if let m = error ?? ride.message {
                    Label(m, systemImage: "exclamationmark.triangle.fill")
                        .font(.subheadline).foregroundStyle(Theme.ink)
                    Button("Try again") { Task { await load() } }.buttonStyle(SecondaryButtonStyle())
                }
                if ride.savedRouteAvailable {
                    Button { ride.startSaved(to: place.coord) } label: {
                        Label("Start saved route (offline)", systemImage: "arrow.down.circle.fill")
                    }
                    .buttonStyle(SecondaryButtonStyle(tint: Theme.accent))
                    .disabled(ride.phase != .idle)
                }
                Button { ride.start(to: place.coord) } label: {
                    HStack(spacing: 10) {
                        if ride.phase == .starting {
                            ProgressView().tint(.black)
                            Text("Downloading the map…")
                        } else {
                            Image(systemName: "location.north.fill")
                            Text("Start ride")
                        }
                    }
                }
                .buttonStyle(PrimaryButtonStyle())
                .disabled(route == nil || error != nil || ride.phase != .idle)
                .opacity(route == nil || error != nil ? 0.45 : 1)
                CubeStatusView(link: ride.cubeLink, cardOnly: true)
            }
        }
    }

    private var divider: some View {
        Rectangle().fill(Theme.hairline).frame(width: 1, height: 40)
    }

    private func stat(_ value: String, _ label: String) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(value).font(Theme.number(22)).foregroundStyle(Theme.ink).lineLimit(1).minimumScaleFactor(0.7)
            Text(label).font(.caption).foregroundStyle(Theme.dim)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.leading, 4)
        .accessibilityElement(children: .combine)
    }

    private func load() async {
        if let demoRoute { route = demoRoute; loading = false; return }
        loading = true; error = nil; ride.clearMessage()
        // The first GPS fix can take a moment.
        for _ in 0..<10 where ride.location.fix == nil { try? await Task.sleep(nanoseconds: 500_000_000) }
        switch await ride.plan(to: place.coord) {
        case .success(let r): route = r
        case .failure(let e):
            error = ride.location.fix == nil ? "Waiting for a GPS fix. Allow location access and try again."
                                             : RideController.text(for: e)
            if e == .noSignal { await ride.offerSavedRoute(to: place.coord) }
        }
        loading = false
    }

    static func km(_ m: Double) -> String { m < 1000 ? "\(Int(m)) m" : String(format: "%.1f km", m / 1000) }
}

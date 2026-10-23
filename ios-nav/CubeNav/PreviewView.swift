import MapKit
import SwiftUI
import NavCore

struct PreviewView: View {
    @EnvironmentObject var ride: RideController
    let place: Place
    @State private var route: Route?
    @State private var error: String?
    @State private var loading = true

    var body: some View {
        VStack(spacing: 0) {
            Map {
                if let route {
                    MapPolyline(coordinates: route.points.map { CLLocationCoordinate2D(latitude: $0.lat, longitude: $0.lon) })
                        .stroke(.blue, lineWidth: 5)
                }
                Marker(place.name, coordinate: CLLocationCoordinate2D(latitude: place.lat, longitude: place.lon))
                UserAnnotation()
            }
            .mapControls { MapCompass(); MapUserLocationButton() }
            VStack(alignment: .leading, spacing: 10) {
                Text(place.name).font(.title3.bold())
                if loading { ProgressView("Finding the route") }
                if let route {
                    Text("\(Self.km(route.distance)) · \(formatETA(route.time))").font(.headline)
                }
                if let m = error ?? ride.message {
                    Text(m).font(.subheadline).foregroundStyle(.red)
                    Button("Try again") { Task { await load() } }.buttonStyle(.bordered)
                }
                if ride.savedRouteAvailable {
                    Button {
                        ride.startSaved(to: place.coord)
                    } label: {
                        Text("Start saved route (offline)").frame(maxWidth: .infinity)
                    }
                    .buttonStyle(.bordered).controlSize(.large)
                    .disabled(ride.phase != .idle)
                }
                Button {
                    ride.start(to: place.coord)
                } label: {
                    Text(ride.phase == .starting ? "Downloading the map…" : "Start").frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent).controlSize(.large)
                .disabled(route == nil || error != nil || ride.phase != .idle)
                CubeStatusView(link: ride.cubeLink)
            }
            .padding()
        }
        .navigationTitle("Route")
        .navigationBarTitleDisplayMode(.inline)
        .task { await load() }
        // Leaving the preview while the map downloads abandons the Start.
        .onDisappear { ride.cancelStart() }
    }

    private func load() async {
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

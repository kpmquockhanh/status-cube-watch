import MapKit
import SwiftUI
import NavCore

struct PreviewView: View {
    @EnvironmentObject var ride: RideController
    @Environment(\.dismiss) private var dismiss
    let place: Place
    /// DEBUG demo only: fixed routes instead of asking the server.
    var demoRoutes: [RouteStyle: Route]? = nil
    /// The style of the last ride started, so the next preview picks it again.
    @AppStorage("routeStyle") private var lastStyle = ""
    @State private var routes: [RouteStyle: Route] = [:]
    /// The style of the row the rider tapped; nil = the default pick.
    @State private var tapped: RouteStyle?
    @State private var error: String?
    @State private var loading = true
    /// Bumped by Try again: restarts the load task (leaving the screen cancels it).
    @State private var attempt = 0
    @State private var camera: MapCameraPosition = .automatic

    private var options: [RouteOption] { routeOptions(routes) }

    private var pick: Int? {
        let o = options
        if let tapped, let i = o.firstIndex(where: { $0.styles.contains(tapped) }) { return i }
        return defaultPick(o, last: RouteStyle(rawValue: lastStyle))
    }

    var body: some View {
        ZStack(alignment: .top) {
            Map(position: $camera) {
                let o = options, p = pick
                // The other routes first, so the picked one is drawn on top.
                ForEach(Array(o.indices.filter { $0 != p }), id: \.self) { i in
                    MapPolyline(coordinates: Self.line(o[i].route))
                        .stroke(Color(hex: 0x5A6170), style: StrokeStyle(lineWidth: 5, lineCap: .round, lineJoin: .round))
                }
                if let p {
                    let line = Self.line(o[p].route)
                    MapPolyline(coordinates: line).stroke(Theme.bg, style: StrokeStyle(lineWidth: 11, lineCap: .round, lineJoin: .round))
                    MapPolyline(coordinates: line).stroke(Theme.route, style: StrokeStyle(lineWidth: 7, lineCap: .round, lineJoin: .round))
                }
                Marker(place.name, systemImage: "flag.checkered", coordinate: CLLocationCoordinate2D(latitude: place.lat, longitude: place.lon))
                    .tint(Theme.accent)
                UserAnnotation()
            }
            .mapStyle(.standard(elevation: .flat, emphasis: .muted, pointsOfInterest: .excludingAll))
            .mapControls { MapCompass() }
            .onChange(of: routes.count) { withAnimation(.easeOut(duration: 0.3)) { fitCamera() } }
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
        .task(id: attempt) { await load() }
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
                let o = options, p = pick
                if o.isEmpty && loading {
                    HStack(spacing: 10) {
                        ProgressView().tint(Theme.accent)
                        Text("Finding a motorbike route…").font(.subheadline).foregroundStyle(Theme.dim)
                    }
                    .frame(minHeight: 58)
                } else if !o.isEmpty {
                    VStack(spacing: 8) {
                        ForEach(o.indices, id: \.self) { i in row(o[i], picked: i == p) }
                    }
                }
                if let m = error ?? ride.message {
                    Label(m, systemImage: "exclamationmark.triangle.fill")
                        .font(.subheadline).foregroundStyle(Theme.ink)
                    Button("Try again") { attempt += 1 }.buttonStyle(SecondaryButtonStyle())
                }
                if ride.savedRouteAvailable {
                    Button { ride.startSaved(to: place.coord) } label: {
                        Label("Start saved route (offline)", systemImage: "arrow.down.circle.fill")
                    }
                    .buttonStyle(SecondaryButtonStyle(tint: Theme.accent))
                    .disabled(ride.phase != .idle)
                }
                Button {
                    guard let p else { return }
                    ride.start(to: place.coord, option: o[p])
                    lastStyle = o[p].style.rawValue
                } label: {
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
                .disabled(p == nil || ride.phase != .idle)
                .opacity(p == nil ? 0.45 : 1)
                CubeStatusView(link: ride.cubeLink, cardOnly: true)
            }
        }
    }

    private func row(_ o: RouteOption, picked: Bool) -> some View {
        Button { tapped = o.style } label: {
            HStack(spacing: 12) {
                Circle().fill(Self.color(o.style)).frame(width: 10, height: 10)
                VStack(alignment: .leading, spacing: 2) {
                    Text(o.title).font(.headline).foregroundStyle(Theme.ink).lineLimit(1).minimumScaleFactor(0.8)
                    Text(routeFacts(o.route.stats)).font(.caption).foregroundStyle(Theme.dim)
                        .lineLimit(1).minimumScaleFactor(0.8)
                }
                Spacer(minLength: 8)
                Text(formatETA(o.route.time)).font(Theme.number(17)).foregroundStyle(Theme.ink).lineLimit(1)
            }
            .padding(.horizontal, 12).padding(.vertical, 10)
            .frame(minHeight: 52)
            .background(RoundedRectangle(cornerRadius: 14).fill(picked ? Theme.surfaceHigh : .clear))
            .overlay(RoundedRectangle(cornerRadius: 14)
                .strokeBorder(picked ? Theme.route : Theme.hairline, lineWidth: picked ? 2 : 1))
            .contentShape(RoundedRectangle(cornerRadius: 14))
        }
        .buttonStyle(.plain)
        .accessibilityElement(children: .combine)
        .accessibilityAddTraits(picked ? .isSelected : [])
    }

    /// Plans the three styles at once; each row appears as its route arrives. The error shows only
    /// when every style failed.
    private func load() async {
        if let demoRoutes { routes = demoRoutes; loading = false; return }
        loading = true; error = nil; routes = [:]; ride.clearMessage()
        // The first GPS fix can take a moment.
        for _ in 0..<10 where ride.location.fix == nil { try? await Task.sleep(nanoseconds: 500_000_000) }
        let now = Date(), dest = place.coord
        var firstError: StartError?
        await withTaskGroup(of: (RouteStyle, Result<Route, StartError>).self) { group in
            for style in RouteStyle.allCases {
                group.addTask { (style, await ride.plan(to: dest, style: style, at: now)) }
            }
            for await (style, result) in group {
                switch result {
                case .success(let r): withAnimation(.easeOut(duration: 0.2)) { routes[style] = r }
                case .failure(let e): if firstError == nil { firstError = e }
                }
            }
        }
        if Task.isCancelled { return }
        loading = false
        guard routes.isEmpty, let e = firstError else { return }
        error = ride.location.fix == nil ? "Waiting for a GPS fix. Allow location access and try again."
                                         : RideController.text(for: e)
        if e == .noSignal { await ride.offerSavedRoute(to: place.coord) }
    }

    /// Frames every route and the destination in the part of the map the sheet leaves visible
    /// (about the top 30%), below the top bar.
    private func fitCamera() {
        let pts = routes.values.flatMap { $0.points.map { MKMapPoint(CLLocationCoordinate2D(latitude: $0.lat, longitude: $0.lon)) } }
            + [MKMapPoint(CLLocationCoordinate2D(latitude: place.lat, longitude: place.lon))]
        guard pts.count > 1 else { return }
        let xs = pts.map(\.x), ys = pts.map(\.y)
        var r = MKMapRect(x: xs.min()!, y: ys.min()!, width: xs.max()! - xs.min()!, height: ys.max()! - ys.min()!)
        r = r.insetBy(dx: -r.width * 0.12 - 200, dy: -r.height * 0.12 - 200)
        // MKMapPoint y grows southward: grow the rect down so the routes sit above the sheet.
        let top = r.height * 0.35
        camera = .rect(MKMapRect(x: r.minX, y: r.minY - top, width: r.width, height: (r.height + top) / 0.3))
    }

    static func color(_ s: RouteStyle) -> Color {
        switch s {
        case .calm: return Theme.good
        case .fastest: return Theme.route
        case .fewestTurns: return Theme.warn
        }
    }

    static func line(_ r: Route) -> [CLLocationCoordinate2D] {
        r.points.map { CLLocationCoordinate2D(latitude: $0.lat, longitude: $0.lon) }
    }
}

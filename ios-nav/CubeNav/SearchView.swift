import MapKit
import SwiftUI
import NavCore

struct Place: Codable, Hashable, Identifiable {
    var name: String
    var subtitle: String
    var lat: Double
    var lon: Double
    var id: String { "\(lat),\(lon)" }
    var coord: Coord { Coord(lat: lat, lon: lon) }
}

enum Recents {
    private static let key = "recents"
    static func load() -> [Place] {
        guard let d = UserDefaults.standard.data(forKey: key), let p = try? JSONDecoder().decode([Place].self, from: d) else { return [] }
        return p
    }
    static func add(_ p: Place) {
        var all = load().filter { $0.id != p.id }
        all.insert(p, at: 0)
        if let d = try? JSONEncoder().encode(Array(all.prefix(10))) { UserDefaults.standard.set(d, forKey: key) }
    }
}

@MainActor
final class Completer: NSObject, ObservableObject, MKLocalSearchCompleterDelegate {
    @Published var results: [MKLocalSearchCompletion] = []
    private let c = MKLocalSearchCompleter()
    override init() {
        super.init()
        c.delegate = self
        c.resultTypes = [.address, .pointOfInterest]
    }
    func query(_ q: String) { if q.isEmpty { results = [] } else { c.queryFragment = q } }
    nonisolated func completerDidUpdateResults(_ completer: MKLocalSearchCompleter) {
        let r = completer.results
        Task { @MainActor in self.results = r }
    }
    nonisolated func completer(_ completer: MKLocalSearchCompleter, didFailWithError error: Error) {}
}

struct SearchView: View {
    @EnvironmentObject var ride: RideController
    @StateObject private var completer = Completer()
    @State private var text = ""
    @State private var recents = Recents.load()
    @State private var resolving = false
    @State private var error: String?
    @FocusState private var searching: Bool
    @Binding var path: [Place]
    var onSettings: () -> Void

    var body: some View {
        ZStack(alignment: .top) {
            Map(initialPosition: .userLocation(fallback: .automatic)) { UserAnnotation() }
                .mapStyle(.standard(elevation: .flat, emphasis: .muted, pointsOfInterest: .excludingAll))
                .mapControls { }
                .ignoresSafeArea()
            LinearGradient(colors: [Theme.bg, Theme.bg.opacity(0)], startPoint: .top, endPoint: .bottom)
                .frame(height: 220).ignoresSafeArea().allowsHitTesting(false)

            VStack(spacing: 12) {
                header
                searchField
                ExpiryBanner()
                if ride.location.denied {
                    notice("Location is off for CubeNav. Allow it in Settings > Privacy & Security > Location Services.",
                           icon: "location.slash.fill")
                }
                if let error { notice(error, icon: "exclamationmark.triangle.fill") }
                Spacer()
                bottomPanel
            }
            .padding(.horizontal, Theme.gutter)
            .padding(.bottom, 8)
        }
        .overlay { if resolving { ProgressView().controlSize(.large).tint(Theme.accent) } }
        .toolbar(.hidden, for: .navigationBar)
        .onChange(of: text) { _, q in completer.query(q) }
        .onAppear { recents = Recents.load() }
        .animation(.spring(response: 0.35, dampingFraction: 0.86), value: text.isEmpty)
    }

    private var header: some View {
        HStack {
            HStack(spacing: 8) {
                Image(systemName: "cube.fill").foregroundStyle(Theme.accent)
                Text("CubeNav").font(.title2.weight(.bold)).foregroundStyle(Theme.ink)
            }
            .accessibilityElement(children: .combine)
            CubeStatusPillOnly(link: ride.cubeLink)
                .lineLimit(1).minimumScaleFactor(0.8)
            Spacer(minLength: 8)
            RoundIconButton(systemName: "gearshape.fill", label: "Settings", action: onSettings)
        }
        .padding(.top, 4)
    }

    private var searchField: some View {
        HStack(spacing: 10) {
            Image(systemName: "magnifyingglass").foregroundStyle(Theme.dim)
            TextField("", text: $text, prompt: Text("Where to?").foregroundStyle(Theme.dim))
                .font(.body.weight(.medium))
                .foregroundStyle(Theme.ink)
                .focused($searching)
                .submitLabel(.search)
                .autocorrectionDisabled()
            if !text.isEmpty {
                Button { text = ""; searching = false } label: {
                    Image(systemName: "xmark.circle.fill").foregroundStyle(Theme.dim)
                        .frame(width: 32, height: 44)
                }
                .accessibilityLabel("Clear search")
            }
        }
        .padding(.horizontal, 16)
        .frame(minHeight: 54)
        .background(Theme.surface, in: RoundedRectangle(cornerRadius: 18, style: .continuous))
        .overlay(RoundedRectangle(cornerRadius: 18, style: .continuous)
            .strokeBorder(searching ? Theme.accent.opacity(0.7) : Theme.hairline, lineWidth: searching ? 1.5 : 1))
        .shadow(color: .black.opacity(0.35), radius: 16, y: 6)
    }

    @ViewBuilder private var bottomPanel: some View {
        if text.isEmpty {
            VStack(alignment: .leading, spacing: 14) {
                CubeStatusView(link: ride.cubeLink, cardOnly: true)
                if !recents.isEmpty {
                    Panel(padding: 6) {
                        VStack(alignment: .leading, spacing: 0) {
                            SectionLabel("Recent").padding(.horizontal, 12).padding(.top, 10).padding(.bottom, 4)
                            ForEach(Array(recents.prefix(4).enumerated()), id: \.element.id) { i, p in
                                if i > 0 { Divider().overlay(Theme.hairline).padding(.leading, 56) }
                                Button { path.append(p) } label: {
                                    row(p.name, p.subtitle, icon: "clock.arrow.circlepath", tint: Theme.dim)
                                }
                                .buttonStyle(RowStyle())
                            }
                        }
                    }
                } else {
                    Panel {
                        Label("Search a place to plan a motorbike route. The map is downloaded before you ride, so it keeps working without signal.",
                              systemImage: "map.fill")
                            .font(.subheadline).foregroundStyle(Theme.dim)
                    }
                }
            }
            .transition(.move(edge: .bottom).combined(with: .opacity))
        } else {
            Panel(padding: 6) {
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 0) {
                        if completer.results.isEmpty {
                            Text("Searching…").font(.subheadline).foregroundStyle(Theme.dim).padding(14)
                        }
                        ForEach(Array(completer.results.enumerated()), id: \.offset) { i, r in
                            if i > 0 { Divider().overlay(Theme.hairline).padding(.leading, 56) }
                            Button { Task { await resolve(r) } } label: {
                                row(r.title, r.subtitle, icon: "mappin.circle.fill", tint: Theme.route)
                            }
                            .buttonStyle(RowStyle())
                        }
                    }
                }
                .frame(maxHeight: 380)
                .scrollDismissesKeyboard(.immediately)
            }
            .transition(.move(edge: .bottom).combined(with: .opacity))
        }
    }

    private func row(_ title: String, _ sub: String, icon: String, tint: Color) -> some View {
        HStack(spacing: 14) {
            Image(systemName: icon).font(.title3).foregroundStyle(tint).frame(width: 30)
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.body.weight(.semibold)).foregroundStyle(Theme.ink).lineLimit(1)
                if !sub.isEmpty { Text(sub).font(.subheadline).foregroundStyle(Theme.dim).lineLimit(1) }
            }
            Spacer(minLength: 0)
            Image(systemName: "chevron.right").font(.footnote.weight(.semibold)).foregroundStyle(Theme.dim.opacity(0.6))
        }
        .padding(.horizontal, 12).padding(.vertical, 12)
        .contentShape(Rectangle())
    }

    private func notice(_ text: String, icon: String) -> some View {
        Label(text, systemImage: icon)
            .font(.subheadline).foregroundStyle(Theme.ink)
            .padding(12).frame(maxWidth: .infinity, alignment: .leading)
            .background(Theme.surface, in: RoundedRectangle(cornerRadius: 16, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 16, style: .continuous).strokeBorder(Theme.warn.opacity(0.5)))
    }

    private func resolve(_ r: MKLocalSearchCompletion) async {
        resolving = true; error = nil
        defer { resolving = false }
        do {
            let resp = try await MKLocalSearch(request: MKLocalSearch.Request(completion: r)).start()
            guard let item = resp.mapItems.first else { error = "Could not find that place."; return }
            let c = item.placemark.coordinate
            let p = Place(name: r.title, subtitle: r.subtitle, lat: c.latitude, lon: c.longitude)
            Recents.add(p)
            searching = false
            path.append(p)
        } catch { self.error = "Search needs a connection. Try again." }
    }
}

/// A list row inside a panel: highlight on press.
struct RowStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .background(configuration.isPressed ? Theme.surfaceHigh : Color.clear,
                        in: RoundedRectangle(cornerRadius: 14, style: .continuous))
    }
}

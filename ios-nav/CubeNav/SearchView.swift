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
    @Binding var path: [Place]

    var body: some View {
        List {
            Section { ExpiryBanner().listRowInsets(EdgeInsets()).listRowBackground(Color.clear) }
                .listSectionSeparator(.hidden)
            Section("Cube") { CubeStatusView(link: ride.cubeLink) }
            if ride.location.denied {
                Section { Text("Location is off for CubeNav. Allow it in Settings > Privacy & Security > Location Services.")
                    .font(.subheadline) }
            }
            if let error { Section { Text(error).foregroundStyle(.red) } }
            if text.isEmpty {
                if !recents.isEmpty {
                    Section("Recent") {
                        ForEach(recents) { p in
                            Button { path.append(p) } label: { row(p.name, p.subtitle) }
                        }
                    }
                }
            } else {
                Section("Results") {
                    ForEach(completer.results, id: \.self) { r in
                        Button { Task { await resolve(r) } } label: { row(r.title, r.subtitle) }
                    }
                }
            }
        }
        .searchable(text: $text, prompt: "Where to?")
        .onChange(of: text) { _, q in completer.query(q) }
        .overlay { if resolving { ProgressView() } }
        .navigationTitle("CubeNav")
        .onAppear { recents = Recents.load() }
    }

    private func row(_ title: String, _ sub: String) -> some View {
        VStack(alignment: .leading) {
            Text(title).foregroundStyle(.primary)
            if !sub.isEmpty { Text(sub).font(.caption).foregroundStyle(.secondary) }
        }
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
            path.append(p)
        } catch { self.error = "Search needs a connection. Try again." }
    }
}

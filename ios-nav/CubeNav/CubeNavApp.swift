import SwiftUI
import NavCore

@main
struct CubeNavApp: App {
    /// Touched here so the one `CubeLink` (and its restorable central manager) exists at launch.
    private let ride = RideController.shared
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(ride)
                .onChange(of: scenePhase, initial: true) { _, p in if p == .active { ride.appBecameActive() } }
        }
    }
}

struct ContentView: View {
    @EnvironmentObject var ride: RideController
    @State private var path: [Place] = []
    @State private var showSettings = false

    var body: some View {
        if ride.phase == .riding {
            RideView().onAppear { path = [] }
        } else {
            NavigationStack(path: $path) {
                SearchView(path: $path)
                    .toolbar {
                        ToolbarItem(placement: .topBarTrailing) {
                            Button { showSettings = true } label: { Image(systemName: "gearshape") }
                        }
                    }
                    .navigationDestination(for: Place.self) { PreviewView(place: $0) }
            }
            .sheet(isPresented: $showSettings) { SettingsView().environmentObject(ride) }
        }
    }
}

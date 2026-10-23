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
                .preferredColorScheme(.dark)
                .tint(Theme.accent)
                .onChange(of: scenePhase, initial: true) { _, p in if p == .active { ride.appBecameActive() } }
        }
    }
}

struct ContentView: View {
    @EnvironmentObject var ride: RideController
    @State private var path: [Place] = []
    @State private var showSettings = false

    var body: some View {
        Group {
            #if DEBUG
            if let demo = DemoScreen.requested {
                demo.view(ride)
            } else {
                main
            }
            #else
            main
            #endif
        }
        .background(Theme.bg)
    }

    @ViewBuilder private var main: some View {
        if ride.phase == .riding {
            RideView().onAppear { path = [] }
                .transition(.opacity)
        } else {
            NavigationStack(path: $path) {
                SearchView(path: $path, onSettings: { showSettings = true })
                    .navigationDestination(for: Place.self) { PreviewView(place: $0) }
            }
            .sheet(isPresented: $showSettings) { SettingsView().environmentObject(ride) }
        }
    }
}

#if DEBUG
/// `-demo ride|offroute|arrived|preview` launch argument: shows a screen with sample data, for
/// simulator screenshots of states that need a real ride.
enum DemoScreen: String {
    case ride, offroute, arrived, preview

    static var requested: DemoScreen? {
        let args = ProcessInfo.processInfo.arguments
        guard let i = args.firstIndex(of: "-demo"), i + 1 < args.count else { return nil }
        return DemoScreen(rawValue: args[i + 1])
    }

    @MainActor @ViewBuilder func view(_ ride: RideController) -> some View {
        switch self {
        case .preview:
            NavigationStack {
                PreviewView(place: Place(name: "Chợ Bến Thành", subtitle: "Lê Lợi, Bến Thành, Quận 1",
                                         lat: 10.7725, lon: 106.6980),
                            demoRoutes: [
                                .calm: Route(points: [Coord(lat: 10.7769, lon: 106.7009), Coord(lat: 10.7781, lon: 106.6990),
                                                      Coord(lat: 10.7752, lon: 106.6962), Coord(lat: 10.7725, lon: 106.6980)],
                                             distance: 1420, time: 372, instructions: [],
                                             stats: RouteStats(turns: 6, lights: 2, mainRoadShare: 0.08)),
                                .fastest: Route(points: [Coord(lat: 10.7769, lon: 106.7009), Coord(lat: 10.7757, lon: 106.7019),
                                                         Coord(lat: 10.7741, lon: 106.6998), Coord(lat: 10.7733, lon: 106.6990),
                                                         Coord(lat: 10.7725, lon: 106.6980)],
                                                distance: 1240, time: 312, instructions: [],
                                                stats: RouteStats(turns: 4, lights: 5, mainRoadShare: 0.62)),
                                .fewestTurns: Route(points: [Coord(lat: 10.7769, lon: 106.7009), Coord(lat: 10.7745, lon: 106.7032),
                                                             Coord(lat: 10.7718, lon: 106.6995), Coord(lat: 10.7725, lon: 106.6980)],
                                                    distance: 1510, time: 345, instructions: [],
                                                    stats: RouteStats(turns: 2, lights: 4, mainRoadShare: 0.41)),
                            ])
            }
        default:
            RideScreen(scene: sample, link: ride.cubeLink, location: ride.location, onStop: {})
        }
    }

    private var sample: NavScene {
        var streets: [SceneStreet] = []
        for i in 0..<9 {
            let y = Int16(30 + i * 34)
            streets.append(SceneStreet(cls: UInt8(i % 4), points: [ScreenPoint(x: -20, y: y), ScreenPoint(x: 260, y: y + Int16(i % 3 * 8))]))
        }
        for i in 0..<6 {
            let x = Int16(10 + i * 46)
            streets.append(SceneStreet(cls: UInt8((i + 1) % 4), points: [ScreenPoint(x: x, y: -20), ScreenPoint(x: x + 12, y: 300)]))
        }
        let route = [ScreenPoint(x: 120, y: 200), ScreenPoint(x: 120, y: 112), ScreenPoint(x: 126, y: 96), ScreenPoint(x: 260, y: 88)]
        switch self {
        case .offroute:
            return NavScene(state: .offRouteNoSignal, turn: .rejoin, exit: 0, dir: 192, gpsWeak: true, dist: "80 m",
                            street: "", eta: "", route: [ScreenPoint(x: -40, y: 140), ScreenPoint(x: 60, y: -40)], streets: streets)
        case .arrived:
            return NavScene(state: .arrived, turn: .arrive, exit: 0, dir: 0, gpsWeak: false, dist: "", street: "Chợ Bến Thành",
                            eta: "", route: [], streets: [])
        default:
            return NavScene(state: .navigating, turn: .right, exit: 0, dir: 0, gpsWeak: false, dist: "150 m",
                            street: "Lê Lợi", eta: "12 min", route: route, streets: streets)
        }
    }
}
#endif

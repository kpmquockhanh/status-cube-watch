import CoreLocation
import Foundation
import NavCore

/// GPS for the ride. Background updates stay on only while a ride runs.
@MainActor
final class LocationService: NSObject, ObservableObject, CLLocationManagerDelegate {
    @Published private(set) var authorization: CLAuthorizationStatus
    @Published private(set) var fix: Fix?
    @Published private(set) var fixAt: Date?
    /// Called for every valid fix.
    var onFix: ((Fix) -> Void)?

    private let manager = CLLocationManager()

    override init() {
        authorization = manager.authorizationStatus
        super.init()
        manager.delegate = self
        manager.desiredAccuracy = kCLLocationAccuracyBestForNavigation
        manager.activityType = .otherNavigation
        manager.pausesLocationUpdatesAutomatically = false
    }

    var denied: Bool { authorization == .denied || authorization == .restricted }

    /// Foreground fixes for the map and for planning a route.
    func begin() {
        if authorization == .notDetermined { manager.requestWhenInUseAuthorization() }
        if authorization == .authorizedWhenInUse || authorization == .authorizedAlways { manager.startUpdatingLocation() }
    }

    /// Asked at the first Start so iOS lets the ride go on with the phone locked.
    func requestAlways() {
        if authorization == .notDetermined || authorization == .authorizedWhenInUse { manager.requestAlwaysAuthorization() }
    }

    func startRide() {
        manager.allowsBackgroundLocationUpdates = true
        manager.showsBackgroundLocationIndicator = true
        manager.startUpdatingLocation()
    }

    func endRide() {
        manager.allowsBackgroundLocationUpdates = false
        manager.showsBackgroundLocationIndicator = false
        if authorization != .authorizedAlways && authorization != .authorizedWhenInUse { manager.stopUpdatingLocation() }
    }

    nonisolated func locationManagerDidChangeAuthorization(_ m: CLLocationManager) {
        let s = m.authorizationStatus
        Task { @MainActor in
            self.authorization = s
            if s == .authorizedWhenInUse || s == .authorizedAlways { self.manager.startUpdatingLocation() }
        }
    }

    nonisolated func locationManager(_ m: CLLocationManager, didUpdateLocations locs: [CLLocation]) {
        guard let l = locs.last, l.horizontalAccuracy >= 0 else { return }
        let f = Fix(coord: Coord(lat: l.coordinate.latitude, lon: l.coordinate.longitude),
                    accuracy: l.horizontalAccuracy, speed: l.speed, course: l.course)
        Task { @MainActor in
            self.fix = f; self.fixAt = Date()
            self.onFix?(f)
        }
    }

    nonisolated func locationManager(_ m: CLLocationManager, didFailWithError error: Error) {}
}

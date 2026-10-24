import CoreLocation
import Foundation
import NavCore

/// GPS for the ride. Background updates stay on only while a ride runs.
@MainActor
final class LocationService: NSObject, ObservableObject, CLLocationManagerDelegate {
    @Published private(set) var authorization: CLAuthorizationStatus
    @Published private(set) var fix: Fix?
    @Published private(set) var fixAt: Date?
    /// A plain-words problem with location, nil when fixes arrive.
    @Published private(set) var problem: String?
    /// Called for every valid fix.
    var onFix: ((Fix) -> Void)?
    /// Called for every usable compass reading during a ride, degrees from north.
    var onHeading: ((Double) -> Void)?

    /// A compass reading less accurate than this is dropped (iOS wants calibration).
    nonisolated static let maxHeadingError = 30.0

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
        // The compass turns the map while the rider is too slow for a GPS course (`RideEngine.compass`).
        if CLLocationManager.headingAvailable() {
            manager.headingFilter = 2
            manager.startUpdatingHeading()
        }
    }

    func endRide() {
        manager.stopUpdatingHeading()
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
            self.fix = f; self.fixAt = Date(); self.problem = nil
            self.onFix?(f)
        }
    }

    nonisolated func locationManager(_ m: CLLocationManager, didUpdateHeading h: CLHeading) {
        guard h.headingAccuracy >= 0, h.headingAccuracy <= Self.maxHeadingError else { return }
        let deg = h.trueHeading >= 0 ? h.trueHeading : h.magneticHeading
        Task { @MainActor in self.onHeading?(deg) }
    }

    nonisolated func locationManager(_ m: CLLocationManager, didFailWithError error: Error) {
        guard (error as? CLError)?.code == .denied else { return }   // .locationUnknown is transient
        Task { @MainActor in self.problem = "Location access was turned off. Allow it in Settings > Privacy & Security > Location Services." }
    }
}

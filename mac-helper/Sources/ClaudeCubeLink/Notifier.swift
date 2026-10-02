import CubeLinkCore
import Foundation
import UserNotifications

/// Posts the Pomodoro banner. Needs the app bundle: `UNUserNotificationCenter.current()`
/// traps in an unbundled `swift run` binary, so every call is a no-op without one.
final class Notifier: NSObject, UNUserNotificationCenterDelegate {
    /// True when the user allows notifications; called on the main queue.
    var onAuthorizationChange: (Bool) -> Void = { _ in }

    private var center: UNUserNotificationCenter? {
        Bundle.main.bundleIdentifier == nil ? nil : UNUserNotificationCenter.current()
    }

    func start() {
        guard let center else {
            Trace.log("notify", "no app bundle: notifications disabled")
            return
        }
        center.delegate = self
        center.requestAuthorization(options: [.alert, .sound]) { [weak self] granted, error in
            Trace.log("notify", "authorization granted \(granted), error \(error?.localizedDescription ?? "none")")
            self?.refreshAuthorization()
        }
    }

    func refreshAuthorization() {
        guard let center else { return }
        center.getNotificationSettings { [weak self] s in
            let ok = s.authorizationStatus == .authorized || s.authorizationStatus == .provisional
            DispatchQueue.main.async { self?.onAuthorizationChange(ok) }
        }
    }

    func post(_ notice: PomodoroNotice) {
        guard let center else { return }
        let c = UNMutableNotificationContent()
        c.title = notice.title
        c.body = notice.body
        c.sound = .default
        // One fixed identifier: a newer phase end replaces the banner instead of stacking.
        center.add(UNNotificationRequest(identifier: "pomodoro-phase-end", content: c, trigger: nil)) { error in
            if let error { Trace.log("notify", "post failed: \(error.localizedDescription)") }
        }
    }

    // An accessory (menu bar) app counts as foreground; without this the banner is swallowed.
    func userNotificationCenter(_ center: UNUserNotificationCenter, willPresent notification: UNNotification,
                                withCompletionHandler done: @escaping (UNNotificationPresentationOptions) -> Void) {
        done([.banner, .sound])
    }
}

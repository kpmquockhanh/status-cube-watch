import Foundation

/// What the Mac says when the cube reports a finished Pomodoro phase.
public struct PomodoroNotice: Equatable {
    public let title: String
    public let body: String

    public init(title: String, body: String) {
        self.title = title
        self.body = body
    }

    public static func make(ended: PomodoroPhase, next: PomodoroPhase) -> PomodoroNotice {
        switch (ended, next) {
        case (.focus, .shortBreak): return PomodoroNotice(title: "Focus done", body: "Take a short break")
        case (.focus, .longBreak): return PomodoroNotice(title: "Focus done", body: "Take a long break")
        case (.shortBreak, .focus), (.longBreak, .focus): return PomodoroNotice(title: "Break over", body: "Time to focus")
        default: return PomodoroNotice(title: "Pomodoro", body: "Phase finished")
        }
    }
}

/// What macOS has told us about notification permission. `unknown` = not answered yet.
public enum NoticeAuthorization: Equatable {
    case unknown, allowed, denied
}

/// How the "Pomodoro notifications" menu item looks for a permission state.
/// Only an explicit denial disables it; an unanswered prompt must not claim System Settings is off.
public struct NoticeItemState: Equatable {
    public let title: String
    public let enabled: Bool

    public init(title: String, enabled: Bool) {
        self.title = title
        self.enabled = enabled
    }

    public static func make(_ auth: NoticeAuthorization) -> NoticeItemState {
        auth == .denied
            ? NoticeItemState(title: "Pomodoro notifications (off in System Settings)", enabled: false)
            : NoticeItemState(title: "Pomodoro notifications", enabled: true)
    }
}

/// The menu's "Pomodoro notifications" checkbox. On unless the user turned it off.
public struct PomodoroNoticePrefs {
    private let defaults: UserDefaults
    private let key = "pomodoroNotifications"

    public init(defaults: UserDefaults = .standard) { self.defaults = defaults }

    public var enabled: Bool {
        get { defaults.object(forKey: key) as? Bool ?? true }
        nonmutating set { defaults.set(newValue, forKey: key) }
    }
}

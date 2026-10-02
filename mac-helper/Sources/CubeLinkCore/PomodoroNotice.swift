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

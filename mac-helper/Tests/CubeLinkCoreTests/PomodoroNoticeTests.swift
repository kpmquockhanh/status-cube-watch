import Foundation
import Testing
@testable import CubeLinkCore

@Test func noticeWording() {
    #expect(PomodoroNotice.make(ended: .focus, next: .shortBreak) == PomodoroNotice(title: "Focus done", body: "Take a short break"))
    #expect(PomodoroNotice.make(ended: .focus, next: .longBreak) == PomodoroNotice(title: "Focus done", body: "Take a long break"))
    #expect(PomodoroNotice.make(ended: .shortBreak, next: .focus) == PomodoroNotice(title: "Break over", body: "Time to focus"))
    #expect(PomodoroNotice.make(ended: .longBreak, next: .focus) == PomodoroNotice(title: "Break over", body: "Time to focus"))
}

@Test func unexpectedCombinationFallsBack() {
    let n = PomodoroNotice.make(ended: .shortBreak, next: .longBreak)
    #expect(n == PomodoroNotice(title: "Pomodoro", body: "Phase finished"))
}

@Test func noticeItemOnlyDisablesWhenDenied() {
    #expect(NoticeItemState.make(.allowed) == NoticeItemState(title: "Pomodoro notifications", enabled: true))
    // Not answered yet is not "off in System Settings": the item stays usable.
    #expect(NoticeItemState.make(.unknown) == NoticeItemState(title: "Pomodoro notifications", enabled: true))
    #expect(NoticeItemState.make(.denied) == NoticeItemState(title: "Pomodoro notifications (off in System Settings)", enabled: false))
}

@Test func muteDefaultsOnAndPersists() {
    let defaults = UserDefaults(suiteName: "pomodoro-notice-test-\(UUID().uuidString)")!
    let prefs = PomodoroNoticePrefs(defaults: defaults)
    #expect(prefs.enabled)  // a fresh install notifies
    prefs.enabled = false
    #expect(!PomodoroNoticePrefs(defaults: defaults).enabled)
    prefs.enabled = true
    #expect(PomodoroNoticePrefs(defaults: defaults).enabled)
}

import Foundation
import Testing
@testable import NavCore

private let cal: Calendar = {
    var c = Calendar(identifier: .gregorian)
    c.timeZone = TimeZone(identifier: "Asia/Ho_Chi_Minh")!
    return c
}()

/// "yyyy-MM-dd HH:mm" in `cal`'s time zone.
private func at(_ s: String) -> Date {
    let f = DateFormatter()
    f.calendar = cal; f.timeZone = cal.timeZone; f.locale = Locale(identifier: "en_US_POSIX")
    f.dateFormat = "yyyy-MM-dd HH:mm"
    return f.date(from: s)!
}

@Suite struct RouteStyleTests {
    @Test func profiles() {
        #expect(RouteStyle.fastest.profile(rushHour: true) == "moto_vn")
        #expect(RouteStyle.fastest.profile(rushHour: false) == "moto_vn")
        #expect(RouteStyle.calm.profile(rushHour: true) == "moto_calm_rush")
        #expect(RouteStyle.calm.profile(rushHour: false) == "moto_calm")
        #expect(RouteStyle.fewestTurns.profile(rushHour: true) == "moto_turns")
        #expect(RouteStyle.fewestTurns.profile(rushHour: false) == "moto_turns")
        #expect(RouteStyle.allCases.map(\.title) == ["Calm", "Fastest", "Fewest turns"])
    }

    @Test func rushHourEdges() {
        // 2026-10-19 is a Monday.
        #expect(isRushHour(at("2026-10-19 07:00"), calendar: cal))
        #expect(isRushHour(at("2026-10-19 08:59"), calendar: cal))
        #expect(!isRushHour(at("2026-10-19 06:59"), calendar: cal))
        #expect(!isRushHour(at("2026-10-19 09:00"), calendar: cal))
        #expect(!isRushHour(at("2026-10-19 16:29"), calendar: cal))
        #expect(isRushHour(at("2026-10-19 16:30"), calendar: cal))
        #expect(isRushHour(at("2026-10-19 18:59"), calendar: cal))
        #expect(!isRushHour(at("2026-10-19 19:00"), calendar: cal))
        #expect(isRushHour(at("2026-10-23 17:00"), calendar: cal))    // Friday
        #expect(!isRushHour(at("2026-10-24 08:00"), calendar: cal))   // Saturday
        #expect(!isRushHour(at("2026-10-25 17:00"), calendar: cal))   // Sunday
    }
}

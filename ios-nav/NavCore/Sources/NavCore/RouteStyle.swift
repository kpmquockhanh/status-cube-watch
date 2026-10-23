import Foundation

/// The route styles the preview offers; case order is display order.
public enum RouteStyle: String, CaseIterable, Sendable {
    case calm, fastest, fewestTurns

    public var title: String {
        switch self {
        case .calm: return "Calm"
        case .fastest: return "Fastest"
        case .fewestTurns: return "Fewest turns"
        }
    }

    /// The server's GraphHopper profile. Fastest keeps `moto_vn`, the name older apps ask for.
    public func profile(rushHour: Bool) -> String {
        switch self {
        case .calm: return rushHour ? "moto_calm_rush" : "moto_calm"
        case .fastest: return "moto_vn"
        case .fewestTurns: return "moto_turns"
        }
    }
}

/// Monday to Friday, 07:00–09:00 and 16:30–19:00 local time (start inclusive, end exclusive).
public func isRushHour(_ date: Date, calendar: Calendar = .current) -> Bool {
    var cal = Calendar(identifier: .gregorian)
    cal.timeZone = calendar.timeZone
    let c = cal.dateComponents([.weekday, .hour, .minute], from: date)
    guard let weekday = c.weekday, (2...6).contains(weekday) else { return false }
    let minute = (c.hour ?? 0) * 60 + (c.minute ?? 0)
    return (420..<540).contains(minute) || (990..<1140).contains(minute)
}

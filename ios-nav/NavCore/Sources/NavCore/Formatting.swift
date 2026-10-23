import Foundation

/// "Now" under 50 m; otherwise rounded to the nearest 10 m, shown as kilometres with one decimal once that reaches 1000.
public func formatDistance(_ meters: Double) -> String {
    if meters < 50 { return "Now" }
    let r = Int((meters / 10).rounded()) * 10
    if r >= 1000 { return String(format: "%.1f km", Double(r) / 1000) }
    return "\(r) m"
}

/// "12 min" or "1 h 05 min".
public func formatETA(_ seconds: Double) -> String {
    let minutes = max(0, Int((seconds / 60).rounded()))
    if minutes < 60 { return "\(minutes) min" }
    return String(format: "%d h %02d min", minutes / 60, minutes % 60)
}

/// "5 turns · 3 lights · main roads 20%"; the lights part is left out when unknown.
public func routeFacts(_ s: RouteStats) -> String {
    func count(_ n: Int, _ word: String) -> String { "\(n) \(word)\(n == 1 ? "" : "s")" }
    var parts = [count(s.turns, "turn")]
    if let lights = s.lights { parts.append(count(lights, "light")) }
    parts.append("main roads \(Int((s.mainRoadShare * 100).rounded()))%")
    return parts.joined(separator: " · ")
}

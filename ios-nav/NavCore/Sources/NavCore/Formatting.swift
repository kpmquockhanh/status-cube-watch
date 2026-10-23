import Foundation

/// "Now" under 10 m, 50 m steps below 950 m, then kilometres with one decimal.
public func formatDistance(_ meters: Double) -> String {
    if meters < 10 { return "Now" }
    if meters >= 950 { return String(format: "%.1f km", meters / 1000) }
    return "\(Int((meters / 50).rounded()) * 50) m"
}

/// "12 min" or "1 h 05 min".
public func formatETA(_ seconds: Double) -> String {
    let minutes = max(0, Int((seconds / 60).rounded()))
    if minutes < 60 { return "\(minutes) min" }
    return String(format: "%d h %02d min", minutes / 60, minutes % 60)
}

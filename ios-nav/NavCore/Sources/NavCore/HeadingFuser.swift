import Foundation

/// The phone's compass, corrected for how the phone sits in a pocket. While the rider moves, GPS
/// course is the truth and every fix teaches the offset course - compass; when the rider is slow
/// the compass plus that offset says which way the bike faces. Trusted only while the offsets of
/// the last 5 s of riding agree within 20° and the rider moved in the last 2 minutes.
public struct HeadingFuser: Sendable {
    static let window: TimeInterval = 5
    static let maxSpread = 20.0
    static let minSamples = 3
    static let forgetAfter: TimeInterval = 120

    private var samples: [(at: Date, offset: Double)] = []

    public init() {}

    /// A fix while moving: GPS `course` and the `compass` reading at the same moment, degrees.
    public mutating func learn(course: Double, compass: Double, at t: Date) {
        samples.append((t, normalized(course - compass)))
        samples.removeAll { t.timeIntervalSince($0.at) > Self.window }
    }

    /// The learned offset, nil while there is none to trust.
    public func offset(at t: Date) -> Double? {
        guard samples.count >= Self.minSamples, let last = samples.last,
              t.timeIntervalSince(last.at) <= Self.forgetAfter else { return nil }
        // Circular mean, so 359° and 1° average to 0°, not 180°.
        let x = samples.reduce(0) { $0 + cos($1.offset * .pi / 180) }
        let y = samples.reduce(0) { $0 + sin($1.offset * .pi / 180) }
        let mean = normalized(atan2(y, x) * 180 / .pi)
        guard samples.allSatisfy({ angleDiff($0.offset, mean) <= Self.maxSpread }) else { return nil }
        return mean
    }

    /// Which way the bike faces, from a compass reading; nil while the offset is not trusted.
    public func heading(compass: Double, at t: Date) -> Double? {
        offset(at: t).map { normalized(compass + $0) }
    }
}

/// Degrees in [0, 360).
private func normalized(_ d: Double) -> Double {
    let r = d.truncatingRemainder(dividingBy: 360)
    return r < 0 ? r + 360 : r
}

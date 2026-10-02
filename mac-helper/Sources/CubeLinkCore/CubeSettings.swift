import Foundation

/// What the cube lets the Mac edit over BLE (the Settings characteristic).
/// Mirrors firmware/src/settings_json.cpp: same keys, same ranges.
public struct CubeSettings: Equatable {
    // Display
    public var backlight = 160   // 10...255
    public var sleepMin = 15     // 0...240, 0 = never
    public var rotateSec = 0     // 0...255, 0 = off
    public var pollSec = 5       // 2...60
    // Pomodoro
    public var focusMin = 25     // 1...99
    public var shortMin = 5
    public var longMin = 15
    public var sessions = 4      // 1...9
    // Network. The cube never sends its passwords back, only whether they are set.
    public var ssid = ""
    public var bridge = ""
    public var wifiPassSet = false
    public var otaPassSet = false

    public init() {}

    public static let backlightRange = 10...255
    public static let sleepRange = 0...240
    public static let rotateRange = 0...255
    public static let pollRange = 2...60
    public static let minutesRange = 1...99
    public static let sessionsRange = 1...9

    /// Parses what the cube returns when the Settings characteristic is read. Unknown keys are ignored.
    public static func parse(_ data: Data) -> CubeSettings? {
        guard let o = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return nil }
        var s = CubeSettings()
        func int(_ k: String, _ r: ClosedRange<Int>, _ into: inout Int) {
            if let v = (o[k] as? NSNumber)?.intValue, r.contains(v) { into = v }
        }
        int("bl", backlightRange, &s.backlight)
        int("sl", sleepRange, &s.sleepMin)
        int("rt", rotateRange, &s.rotateSec)
        int("pi", pollRange, &s.pollSec)
        int("pf", minutesRange, &s.focusMin)
        int("ps", minutesRange, &s.shortMin)
        int("pl", minutesRange, &s.longMin)
        int("pn", sessionsRange, &s.sessions)
        s.ssid = o["ssid"] as? String ?? ""
        s.bridge = o["bridge"] as? String ?? ""
        s.wifiPassSet = o["wifiPass"] as? Bool ?? false
        s.otaPassSet = o["otaPass"] as? Bool ?? false
        return s
    }

    /// True when every field is inside the range the cube would accept.
    public var isValid: Bool {
        CubeSettings.backlightRange.contains(backlight) && CubeSettings.sleepRange.contains(sleepMin)
            && CubeSettings.rotateRange.contains(rotateSec) && CubeSettings.pollRange.contains(pollSec)
            && CubeSettings.minutesRange.contains(focusMin) && CubeSettings.minutesRange.contains(shortMin)
            && CubeSettings.minutesRange.contains(longMin) && CubeSettings.sessionsRange.contains(sessions)
    }

    /// The JSON to write to change `old` into `self`: only the keys that differ, so a stale window
    /// never overwrites something edited elsewhere. `wifiPass` / `otaPass` are the new passwords the
    /// user typed (nil = leave alone). Returns nil when there is nothing to send.
    public func patch(from old: CubeSettings, wifiPass: String? = nil, otaPass: String? = nil) -> Data? {
        var o: [String: Any] = [:]
        if backlight != old.backlight { o["bl"] = backlight }
        if sleepMin != old.sleepMin { o["sl"] = sleepMin }
        if rotateSec != old.rotateSec { o["rt"] = rotateSec }
        if pollSec != old.pollSec { o["pi"] = pollSec }
        if focusMin != old.focusMin { o["pf"] = focusMin }
        if shortMin != old.shortMin { o["ps"] = shortMin }
        if longMin != old.longMin { o["pl"] = longMin }
        if sessions != old.sessions { o["pn"] = sessions }
        if ssid != old.ssid { o["ssid"] = ssid }
        if bridge != old.bridge { o["bridge"] = bridge }
        if let wifiPass { o["pass"] = wifiPass }
        if let otaPass { o["otapass"] = otaPass }
        guard !o.isEmpty else { return nil }
        return try? JSONSerialization.data(withJSONObject: o, options: [.sortedKeys])
    }

    /// True when applying `patch` makes the cube reboot (it only reads the network settings at boot).
    public static func needsReboot(_ patch: Data) -> Bool {
        guard let o = try? JSONSerialization.jsonObject(with: patch) as? [String: Any] else { return false }
        return !Set(o.keys).isDisjoint(with: ["ssid", "pass", "bridge", "otapass"])
    }
}

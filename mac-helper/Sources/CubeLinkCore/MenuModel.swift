import Foundation

public enum LinkState: Equatable {
    case bluetoothOff, unauthorized, searching, connecting, connected
}

public enum BarTint: Equatable { case normal, amber, red }

public struct CardRow: Equatable {
    public let title: String
    public let detail: String
}

/// Everything the menu bar item shows, derived from plain values so it can be tested without AppKit.
public struct MenuModel: Equatable {
    public static let amberAt = 60  // same notches as the firmware rings
    public static let redAt = 85

    public let barTitle: String
    public let tint: BarTint
    public let cards: [CardRow]
    public let bridgeLine: String
    public let cubeLine: String
    public let cubeConnected: Bool

    public static func make(payload: Data?, bridgeUp: Bool, link: LinkState) -> MenuModel {
        var cards: [CardRow] = []
        var barPercent: Int?
        var barSettled = false  // the bar follows the first ring card only, even when it has no reading
        if let payload,
           let root = try? JSONSerialization.jsonObject(with: payload) as? [String: Any],
           let list = root["cards"] as? [[String: Any]] {
            for c in list {
                let title = c["t"] as? String ?? ""
                let g = (c["g"] as? NSNumber)?.intValue
                let reading = g.flatMap { $0 >= 0 ? $0 : nil }
                if !barSettled, g != nil { barPercent = reading; barSettled = true }
                // The combined usage card carries both windows as `rows` and the unread
                // count as `m`; list each on its own line instead of one "CLAUDE" row.
                if let rows = c["rows"] as? [[String: Any]], !rows.isEmpty {
                    for r in rows {
                        cards.append(CardRow(title: "\(r["k"] as? String ?? "")  \(r["p"] as? String ?? "")",
                                             detail: r["r"] as? String ?? ""))
                    }
                    if let m = c["m"] as? String, !m.isEmpty { cards.append(CardRow(title: "Unread mail", detail: m)) }
                    continue
                }
                let detail = ["v", "s1", "s2"].compactMap { c[$0] as? String }.filter { !$0.isEmpty }
                    .joined(separator: " · ")
                cards.append(CardRow(title: reading.map { "\(title)  \($0)%" } ?? title, detail: detail))
            }
        }
        let tint: BarTint
        switch barPercent ?? 0 {
        case redAt...: tint = .red
        case amberAt...: tint = .amber
        default: tint = .normal
        }
        let cube: String
        switch link {
        case .bluetoothOff: cube = "Cube: Bluetooth is off"
        case .unauthorized: cube = "Cube: Bluetooth permission needed"
        case .searching: cube = "Cube: searching…"
        case .connecting: cube = "Cube: connecting (enter the code on the cube if asked)"
        case .connected: cube = "Cube: connected"
        }
        return MenuModel(barTitle: barPercent.map { "\($0)%" } ?? "–", tint: tint, cards: cards,
                         bridgeLine: bridgeUp ? "Bridge: running" : "Bridge: down",
                         cubeLine: cube, cubeConnected: link == .connected)
    }
}

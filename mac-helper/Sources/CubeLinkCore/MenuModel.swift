import Foundation

public enum LinkState: Equatable {
    case bluetoothOff, unauthorized, searching, connecting, connected
}

public enum BarTint: Equatable { case ink, yellow, orange }

public struct CardRow: Equatable {
    public let title: String      // flat one-line form, "5H  14%"
    public let detail: String
    public let label: String      // "5H"
    public let value: String      // "14%" (or the mail count); empty when there is none
    public let fraction: Double?  // 0...1 when the row has a gauge
    public let tint: BarTint

    public init(title: String, detail: String, label: String? = nil, value: String = "",
                fraction: Double? = nil, tint: BarTint = .ink) {
        self.title = title
        self.detail = detail
        self.label = label ?? title
        self.value = value
        self.fraction = fraction
        self.tint = tint
    }
}

/// One Agents tile as the bridge sends it (`a` in the Agents card): every word is the bridge's.
public struct AgentTile: Equatable {
    public let id: String
    public let name: String     // `n`, cut to 16 by the bridge
    public let line: String     // `l`, the status line ("edit · 41% ctx")
    public let kind: String     // `k`: run|wait|idle|err|exit|start
    public let pose: String     // `p`
    public let context: Int     // `x`, 0...100
    public let unseen: Bool     // `u: 1`
    public let hat: Int?        // `h`, shape * 8 + colour
    public let subagents: String?  // `s`: "" one, "×N" more, nil none

    public init(id: String, name: String, line: String, kind: String, pose: String, context: Int,
                unseen: Bool = false, hat: Int? = nil, subagents: String? = nil) {
        self.id = id; self.name = name; self.line = line; self.kind = kind; self.pose = pose
        self.context = context; self.unseen = unseen; self.hat = hat; self.subagents = subagents
    }

    /// Finished a turn nobody looked at: striped, as on the cube and the board.
    public var hungry: Bool { pose == "hungry" || (kind == "idle" && unseen) }
}

/// The Agents card, for the menu's tile grid.
public struct AgentsSection: Equatable {
    public static let maxTiles = 4

    public let caption: String  // `s1`, "1 NEEDS YOU"
    public let tiles: [AgentTile]  // every tile, in the bridge's order

    public init(caption: String, tiles: [AgentTile]) {
        self.caption = caption
        self.tiles = tiles
    }

    /// The tiles the menu shows, at most `maxTiles`, in the bridge's order (its tmux panes, left to right):
    /// waiting ones always get a place, the others fill the rest from the start.
    public var shown: [AgentTile] {
        let waiting = tiles.filter { $0.kind == "wait" }.prefix(Self.maxTiles)
        var others = Self.maxTiles - waiting.count
        return tiles.filter { t in
            if t.kind == "wait" { return waiting.contains(t) }
            others -= 1
            return others >= 0
        }
    }
    public var hidden: Int { max(0, tiles.count - Self.maxTiles) }
    public var waiting: Bool { tiles.contains { $0.kind == "wait" } }
}

/// Everything the menu bar item shows, derived from plain values so it can be tested without AppKit.
public struct MenuModel: Equatable {
    public static let yellowAt = 60  // same notches and tones as the firmware rings: ink, yellow, orange
    public static let orangeAt = 85

    public let barTitle: String
    public let tint: BarTint
    public let cards: [CardRow]
    public let agents: AgentsSection?  // nil: no Agents card (tracking off, or no payload)
    public let bridgeLine: String
    public let cubeLine: String
    public let cubeConnected: Bool
    public let bridgeUp: Bool
    public let link: LinkState

    public static func tint(percent: Int) -> BarTint {
        switch percent {
        case orangeAt...: return .orange
        case yellowAt...: return .yellow
        default: return .ink
        }
    }

    public static func make(payload: Data?, bridgeUp: Bool, link: LinkState) -> MenuModel {
        var cards: [CardRow] = []
        var agents: AgentsSection?
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
                        let k = r["k"] as? String ?? "", p = r["p"] as? String ?? ""
                        let pct = Int(p.filter(\.isNumber))
                        cards.append(CardRow(title: "\(k)  \(p)", detail: r["r"] as? String ?? "", label: k, value: p,
                                             fraction: pct.map { Double(min(max($0, 0), 100)) / 100 },
                                             tint: pct.map(tint(percent:)) ?? .ink))
                    }
                    if let m = c["m"] as? String, !m.isEmpty {
                        cards.append(CardRow(title: "Unread mail", detail: m, label: "Unread mail", value: m))
                    }
                    continue
                }
                // The Agents card (it carries the tile list `a`, and no gauge): the tile grid, not a row.
                // The prompt `q` is not shown here.
                if let list = c["a"] as? [[String: Any]], g == nil {
                    agents = AgentsSection(caption: c["s1"] as? String ?? "", tiles: list.compactMap(tile))
                    continue
                }
                let detail = ["v", "s1", "s2"].compactMap { c[$0] as? String }.filter { !$0.isEmpty }
                    .joined(separator: " · ")
                cards.append(CardRow(title: reading.map { "\(title)  \($0)%" } ?? title, detail: detail, label: title,
                                     value: reading.map { "\($0)%" } ?? "",
                                     fraction: reading.map { Double(min($0, 100)) / 100 },
                                     tint: reading.map(tint(percent:)) ?? .ink))
            }
        }
        let tint = tint(percent: barPercent ?? 0)
        let cube: String
        switch link {
        case .bluetoothOff: cube = "Cube: Bluetooth is off"
        case .unauthorized: cube = "Cube: Bluetooth permission needed"
        case .searching: cube = "Cube: searching…"
        case .connecting: cube = "Cube: connecting (enter the code on the cube if asked)"
        case .connected: cube = "Cube: connected"
        }
        return MenuModel(barTitle: barPercent.map { "\($0)%" } ?? "–", tint: tint, cards: cards, agents: agents,
                         bridgeLine: bridgeUp ? "Bridge: running" : "Bridge: down",
                         cubeLine: cube, cubeConnected: link == .connected, bridgeUp: bridgeUp, link: link)
    }

    static func tile(_ t: [String: Any]) -> AgentTile? {
        guard let id = t["i"] as? String ?? (t["i"] as? NSNumber)?.stringValue else { return nil }
        return AgentTile(id: id, name: t["n"] as? String ?? "", line: t["l"] as? String ?? "", kind: t["k"] as? String ?? "",
                         pose: t["p"] as? String ?? "", context: min(max((t["x"] as? NSNumber)?.intValue ?? 0, 0), 100),
                         unseen: (t["u"] as? NSNumber)?.intValue == 1, hat: (t["h"] as? NSNumber)?.intValue,
                         subagents: t["s"] as? String)
    }
}

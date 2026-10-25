import Foundation

/// How often the app fetches `/api/status`: every second while an agent waits for an answer
/// (so a prompt reaches the cube quickly), else at the usual pace.
public enum PollPolicy {
    public static let waiting: TimeInterval = 1
    public static let idle: TimeInterval = 5

    /// True when a card with an `a` array holds a tile whose kind `k` is `wait`.
    public static func agentWaiting(payload: Data?) -> Bool {
        guard let payload,
              let o = try? JSONSerialization.jsonObject(with: payload) as? [String: Any],
              let cards = o["cards"] as? [[String: Any]] else { return false }
        return cards.contains { card in
            (card["a"] as? [[String: Any]])?.contains { $0["k"] as? String == "wait" } ?? false
        }
    }

    /// A detail screen open on the cube needs fresh numbers each second, like a waiting prompt.
    public static func interval(payload: Data?, detailOpen: Bool = false) -> TimeInterval {
        detailOpen || agentWaiting(payload: payload) ? waiting : idle
    }
}

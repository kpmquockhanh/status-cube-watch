import Foundation

/// The bridge's `GET /api/board` (`cards.BuildBoard`): every agent, not only the Agents card's 8, in the tiles'
/// order. The screen saver draws it. Each agent carries the tile fields `{i,n,l,k,p,x,u,h,s}` (read as the menu
/// reads `a`) plus `d` and `q`, which are not read here.
public struct Board: Equatable {
    public let caption: String  // `s1`, "2 NEED YOU", "NO AGENTS"
    public let tiles: [AgentTile]

    public init(caption: String, tiles: [AgentTile]) {
        self.caption = caption
        self.tiles = tiles
    }

    /// nil for a body that is not the board's JSON.
    public static func parse(_ data: Data) -> Board? {
        guard let o = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let list = o["agents"] as? [[String: Any]] else { return nil }
        return Board(caption: o["s1"] as? String ?? "", tiles: list.compactMap(MenuModel.tile))
    }

    /// The URL of the board's data on the bridge at `port` (loopback only: the bridge serves it to no one else).
    public static func url(port: Int) -> URL { URL(string: "http://127.0.0.1:\(port)/api/board")! }
}

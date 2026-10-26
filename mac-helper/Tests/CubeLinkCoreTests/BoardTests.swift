import Foundation
import Testing
@testable import CubeLinkCore

@Test func boardParsesEveryAgentInOrder() {
    let json = """
    {"ts":1,"s1":"1 NEEDS YOU","c":"accent","agents":[
      {"i":"b","n":"api","l":"allow bash?","k":"wait","p":"wait","x":12,"h":3,"d":{"t":"x"},"q":{"p":"p1"}},
      {"i":"a","n":"web","l":"edit · 41% ctx","k":"run","p":"edit","x":140,"s":"×2"},
      {"n":"no id"},
      {"i":7,"n":"num","l":"","k":"idle","p":"sleep","x":0,"u":1}
    ]}
    """
    let b = try! #require(Board.parse(Data(json.utf8)))
    #expect(b.caption == "1 NEEDS YOU")
    #expect(b.tiles.map(\.id) == ["b", "a", "7"])
    #expect(b.tiles[0] == AgentTile(id: "b", name: "api", line: "allow bash?", kind: "wait", pose: "wait", context: 12, hat: 3))
    #expect(b.tiles[1].context == 100 && b.tiles[1].subagents == "×2")
    #expect(b.tiles[2].hungry)
}

@Test func boardRejectsOtherBodies() {
    #expect(Board.parse(Data("{\"cards\":[]}".utf8)) == nil)
    #expect(Board.parse(Data("nope".utf8)) == nil)
    #expect(Board.parse(Data(#"{"s1":"NO AGENTS","agents":[]}"#.utf8)) == Board(caption: "NO AGENTS", tiles: []))
    #expect(Board.url(port: 8787).absoluteString == "http://127.0.0.1:8787/api/board")
}

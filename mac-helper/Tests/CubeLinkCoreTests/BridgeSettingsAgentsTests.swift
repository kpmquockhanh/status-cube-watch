import Foundation
import Testing
@testable import CubeLinkCore

@Test func agentsEnv() {
    #expect(BridgeSettings().environment(inherited: [:], secrets: BridgeSecrets())["CUBE_AGENTS"] == "1")
    var s = BridgeSettings()
    s.agents = false
    #expect(s.environment(inherited: [:], secrets: BridgeSecrets())["CUBE_AGENTS"] == "0")
    #expect(BridgeSettings().environment(inherited: ["CUBE_AGENTS": "0"], secrets: BridgeSecrets())["CUBE_AGENTS"] == "1")
}

@Test func agentsDecodesMissingAsTrue() throws {
    let s = try JSONDecoder().decode(BridgeSettings.self, from: Data(#"{"lan": false}"#.utf8))
    #expect(s.agents)
}

@Test func agentsImported() throws {
    let (s, _) = try #require(BridgeSettings.imported(configJSON: Data(#"{"agents": false}"#.utf8)))
    #expect(!s.agents)
    let (d, _) = try #require(BridgeSettings.imported(configJSON: Data("{}".utf8)))
    #expect(d.agents)
}

import Testing
@testable import CubeLinkCore

@Test func adoptsAnExternalBridgeInsteadOfFightingForThePort() {
    #expect(BridgeSupervisor.decide(externalBridgeUp: true, node: "/opt/homebrew/bin/node") == .adopt)
    #expect(BridgeSupervisor.decide(externalBridgeUp: true, node: nil) == .adopt)  // no node needed if it is already running
}

@Test func spawnsWhenNothingIsListening() {
    #expect(BridgeSupervisor.decide(externalBridgeUp: false, node: "/opt/homebrew/bin/node") == .spawn(node: "/opt/homebrew/bin/node"))
}

@Test func failsLoudlyWithoutNode() {
    guard case .fail(let why) = BridgeSupervisor.decide(externalBridgeUp: false, node: nil) else {
        Issue.record("expected .fail")
        return
    }
    #expect(why.contains("CUBE_NODE"))
}

@Test func findsNodeFromEnvironmentThenKnownPaths() {
    let present: Set<String> = ["/usr/local/bin/node", "/custom/node"]
    let exists: (String) -> Bool = { present.contains($0) }
    #expect(BridgeSupervisor.findNode(env: ["CUBE_NODE": "/custom/node"], exists: exists) == "/custom/node")
    #expect(BridgeSupervisor.findNode(env: [:], exists: exists) == "/usr/local/bin/node")
    #expect(BridgeSupervisor.findNode(env: ["CUBE_NODE": "/missing/node"], exists: exists) == "/usr/local/bin/node")  // a bad override falls through
    #expect(BridgeSupervisor.findNode(env: [:], exists: { _ in false }) == nil)
}

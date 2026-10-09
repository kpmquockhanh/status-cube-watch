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

@Test func backoffResetsOnlyAfterSixtySecondsUp() {
    #expect(!BridgeSupervisor.shouldResetBackoff(uptime: 59))
    #expect(BridgeSupervisor.shouldResetBackoff(uptime: 60))
}

@Test func recordedNodeComesAfterTheEnvironmentAndBeforeKnownPaths() {
    let present: Set<String> = ["/usr/local/bin/node", "/Users/me/.nvm/versions/node/v22.1.0/bin/node", "/custom/node"]
    let exists: (String) -> Bool = { present.contains($0) }
    let nvm = "/Users/me/.nvm/versions/node/v22.1.0/bin/node"
    #expect(BridgeSupervisor.findNode(env: [:], recorded: nvm, exists: exists) == nvm)  // Finder launch, nvm install
    #expect(BridgeSupervisor.findNode(env: ["CUBE_NODE": "/custom/node"], recorded: nvm, exists: exists) == "/custom/node")
    #expect(BridgeSupervisor.findNode(env: ["CUBE_NODE": ""], recorded: "/gone/node", exists: exists) == "/usr/local/bin/node")
}

@Test func findsTheBridgeFromEnvironmentThenRecordedThenNearby() {
    let present: Set<String> = ["/env/bridge/server.mjs", "/repo/bridge/server.mjs"]
    let exists: (String) -> Bool = { present.contains($0) }
    #expect(BridgeSupervisor.findBridgeDir(env: ["CUBE_BRIDGE_DIR": "/env/bridge"], recorded: "/repo/bridge",
                                           searchFrom: [], exists: exists) == "/env/bridge")
    #expect(BridgeSupervisor.findBridgeDir(env: [:], recorded: "/repo/bridge", searchFrom: ["/"], exists: exists) == "/repo/bridge")
    // A stale CUBE_BRIDGE_DIR (the repo moved) falls through.
    #expect(BridgeSupervisor.findBridgeDir(env: ["CUBE_BRIDGE_DIR": "/old/bridge"], recorded: nil,
                                           searchFrom: ["/repo/mac-helper/build/ClaudeCubeLink.app"], exists: exists) == "/repo/bridge")
    // `swift run`: the bundle path is the directory holding the executable.
    #expect(BridgeSupervisor.findBridgeDir(env: [:], recorded: nil,
                                           searchFrom: ["/", "/repo/mac-helper/.build/arm64-apple-macosx/debug"], exists: exists) == "/repo/bridge")
    #expect(BridgeSupervisor.findBridgeDir(env: [:], recorded: nil, searchFrom: ["/repo"], exists: exists) == "/repo/bridge")
}

@Test func findsTheBridgeInsideTheBundleFirst() {
    let res = "/Users/me/Applications/ClaudeCubeLink.app/Contents/Resources"
    let present: Set<String> = [res + "/bridge/server.mjs", "/repo/bridge/server.mjs"]
    let exists: (String) -> Bool = { present.contains($0) }
    // Finder launch: cwd "/", nothing recorded.
    #expect(BridgeSupervisor.findBridgeDir(env: [:], recorded: nil, searchFrom: [res, "/", "/Users/me/Applications/ClaudeCubeLink.app"],
                                           exists: exists) == res + "/bridge")
    // The environment still overrides it, for working on the bridge.
    #expect(BridgeSupervisor.findBridgeDir(env: ["CUBE_BRIDGE_DIR": "/repo/bridge"], recorded: nil, searchFrom: [res],
                                           exists: exists) == "/repo/bridge")
}

@Test func bridgeSearchStopsAFewLevelsUp() {
    let exists: (String) -> Bool = { $0 == "/a/bridge/server.mjs" }
    #expect(BridgeSupervisor.findBridgeDir(env: [:], recorded: nil, searchFrom: ["/a/b/c/d/e"], exists: exists) == "/a/bridge")
    #expect(BridgeSupervisor.findBridgeDir(env: [:], recorded: nil, searchFrom: ["/a/b/c/d/e/f"], exists: exists) == nil)
    #expect(BridgeSupervisor.findBridgeDir(env: [:], recorded: nil, searchFrom: ["/"], exists: { _ in false }) == nil)
}

@Test func logsADecisionOnlyWhenItChanges() {
    #expect(BridgeSupervisor.announcement(.adopt, previous: nil, port: 8787) == "bridge already running on :8787; using it")
    #expect(BridgeSupervisor.announcement(.adopt, previous: .adopt, port: 8787) == nil)  // the 30 s re-check
    #expect(BridgeSupervisor.announcement(.fail("no node"), previous: .adopt, port: 8787) == "no node")
    #expect(BridgeSupervisor.announcement(.fail("no node"), previous: .fail("no node"), port: 8787) == nil)
    #expect(BridgeSupervisor.announcement(.adopt, previous: .spawn(node: "/n"), port: 8787) != nil)  // our child died, another took over
    #expect(BridgeSupervisor.announcement(.spawn(node: "/n"), previous: nil, port: 8787) == nil)  // spawn() logs the pid itself
}

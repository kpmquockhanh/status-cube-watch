import Testing
@testable import CubeLinkCore

@Test func adoptsAnExternalBridgeInsteadOfFightingForThePort() {
    #expect(BridgeSupervisor.decide(externalBridgeUp: true, binary: "/b/cube-bridge") == .adopt)
    #expect(BridgeSupervisor.decide(externalBridgeUp: true, binary: nil) == .adopt)  // no binary needed if it is already running
}

@Test func spawnsWhenNothingIsListening() {
    #expect(BridgeSupervisor.decide(externalBridgeUp: false, binary: "/b/cube-bridge") == .spawn(binary: "/b/cube-bridge"))
}

@Test func failsLoudlyWithoutTheBinary() {
    guard case .fail(let why) = BridgeSupervisor.decide(externalBridgeUp: false, binary: nil) else {
        Issue.record("expected .fail")
        return
    }
    #expect(why.contains("install.sh install"))
}

@Test func backoffResetsOnlyAfterSixtySecondsUp() {
    #expect(!BridgeSupervisor.shouldResetBackoff(uptime: 59))
    #expect(BridgeSupervisor.shouldResetBackoff(uptime: 60))
}

@Test func findsTheBridgeFromEnvironmentThenRecordedThenNearby() {
    let present: Set<String> = ["/env/bridge/cube-bridge", "/repo/bridge/cube-bridge"]
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
    let present: Set<String> = [res + "/bridge/cube-bridge", "/repo/bridge/cube-bridge"]
    let exists: (String) -> Bool = { present.contains($0) }
    // Finder launch: cwd "/", nothing recorded.
    #expect(BridgeSupervisor.findBridgeDir(env: [:], recorded: nil, searchFrom: [res, "/", "/Users/me/Applications/ClaudeCubeLink.app"],
                                           exists: exists) == res + "/bridge")
    // The environment still overrides it, for working on the bridge.
    #expect(BridgeSupervisor.findBridgeDir(env: ["CUBE_BRIDGE_DIR": "/repo/bridge"], recorded: nil, searchFrom: [res],
                                           exists: exists) == "/repo/bridge")
}

@Test func bridgeSearchStopsAFewLevelsUp() {
    let exists: (String) -> Bool = { $0 == "/a/bridge/cube-bridge" }
    #expect(BridgeSupervisor.findBridgeDir(env: [:], recorded: nil, searchFrom: ["/a/b/c/d/e"], exists: exists) == "/a/bridge")
    #expect(BridgeSupervisor.findBridgeDir(env: [:], recorded: nil, searchFrom: ["/a/b/c/d/e/f"], exists: exists) == nil)
    #expect(BridgeSupervisor.findBridgeDir(env: [:], recorded: nil, searchFrom: ["/"], exists: { _ in false }) == nil)
}

@Test func logsADecisionOnlyWhenItChanges() {
    #expect(BridgeSupervisor.announcement(.adopt, previous: nil, port: 8787) == "bridge already running on :8787; using it")
    #expect(BridgeSupervisor.announcement(.adopt, previous: .adopt, port: 8787) == nil)  // the 30 s re-check
    #expect(BridgeSupervisor.announcement(.fail("no node"), previous: .adopt, port: 8787) == "no node")
    #expect(BridgeSupervisor.announcement(.fail("no node"), previous: .fail("no node"), port: 8787) == nil)
    #expect(BridgeSupervisor.announcement(.adopt, previous: .spawn(binary: "/n"), port: 8787) != nil)  // our child died, another took over
    #expect(BridgeSupervisor.announcement(.spawn(binary: "/n"), previous: nil, port: 8787) == nil)  // spawn() logs the pid itself
}

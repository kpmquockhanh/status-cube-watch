import Foundation
import Testing
@testable import CubeLinkCore

@Test func defaultsGiveTheBridgeItsOwnDefaults() {
    let env = BridgeSettings().environment(inherited: ["PATH": "/usr/bin", "HOME": "/Users/me"], secrets: BridgeSecrets())
    #expect(env == ["PATH": "/usr/bin", "HOME": "/Users/me", "CUBE_SOURCE": "local", "CUBE_EXTRA_CARDS": "0"])
}

@Test func settingsReachTheBridgeAsItsEnvironmentVariables() {
    var s = BridgeSettings()
    s.source = .admin
    s.extraCards = true
    s.refreshSec = 90
    s.lan = false
    s.gmailUser = "me@gmail.com"
    let env = s.environment(inherited: [:], secrets: BridgeSecrets(adminKey: "sk-ant-admin-x", gmailPassword: "abcd efgh"))
    #expect(env == ["CUBE_SOURCE": "admin", "CUBE_EXTRA_CARDS": "1", "CUBE_REFRESH_MS": "90000",
                    "CUBE_HOST": "127.0.0.1", "ANTHROPIC_ADMIN_KEY": "sk-ant-admin-x",
                    "CUBE_GMAIL_USER": "me@gmail.com", "CUBE_GMAIL_PASSWORD": "abcd efgh"])
}

@Test func theAppsOwnEnvironmentCannotSneakSettingsBackIn() {
    // A LaunchAgent or shell that still exports these must not override what the window says.
    let inherited = ["CUBE_HOST": "192.168.1.5", "CUBE_EXTRA_CARDS": "1", "ANTHROPIC_ADMIN_KEY": "old",
                     "CUBE_GMAIL_PASSWORD": "old", "CUBE_TRACE": "1"]
    let env = BridgeSettings().environment(inherited: inherited, secrets: BridgeSecrets())
    #expect(env == ["CUBE_TRACE": "1", "CUBE_SOURCE": "local", "CUBE_EXTRA_CARDS": "0"])
}

@Test func mailNeedsBothHalves() {
    var s = BridgeSettings()
    s.gmailUser = "me@gmail.com"
    #expect(s.problem(secrets: BridgeSecrets()) != nil)
    #expect(s.environment(inherited: [:], secrets: BridgeSecrets())["CUBE_GMAIL_USER"] == nil)
    #expect(s.problem(secrets: BridgeSecrets(gmailPassword: "pw")) == nil)
    #expect(BridgeSettings().problem(secrets: BridgeSecrets(gmailPassword: "pw")) != nil)
}

@Test func theAdminSourceNeedsACredential() {
    var s = BridgeSettings()
    s.source = .admin
    #expect(s.problem(secrets: BridgeSecrets()) != nil)  // server.mjs would exit at startup
    #expect(s.problem(secrets: BridgeSecrets(adminKey: "k")) == nil)
    #expect(s.problem(secrets: BridgeSecrets(oauthToken: "t")) == nil)
    s.refreshSec = 0
    #expect(s.problem(secrets: BridgeSecrets(adminKey: "k")) != nil)
}

@Test func importsABridgeConfigJSON() throws {
    let json = """
    {"source": "local", "port": 8787, "refreshMs": 5000, "extraCards": true,
     "//source": "admin", "gmail": {"user": "me@gmail.com", "appPassword": "abcd efgh"}}
    """
    let (s, secrets) = try #require(BridgeSettings.imported(configJSON: Data(json.utf8)))
    var want = BridgeSettings()
    want.extraCards = true
    want.refreshSec = 5
    want.gmailUser = "me@gmail.com"
    #expect(s == want)
    #expect(secrets == BridgeSecrets(gmailPassword: "abcd efgh"))
}

@Test func importReadsValuesTheWayServerDoes() throws {
    let json = """
    {"source": "admin", "adminKey": "k", "oauthToken": "t", "extraCards": "yes", "refreshMs": "bad", "host": "127.0.0.1"}
    """
    let (s, secrets) = try #require(BridgeSettings.imported(configJSON: Data(json.utf8)))
    #expect(s.source == .admin)
    #expect(s.extraCards)
    #expect(s.refreshSec == nil)  // server.mjs falls back to its default too
    #expect(!s.lan)
    #expect(secrets == BridgeSecrets(adminKey: "k", oauthToken: "t"))
    // A host pinned to a LAN address still serves the LAN.
    #expect(try #require(BridgeSettings.imported(configJSON: Data(#"{"host": "192.168.1.5"}"#.utf8))).0.lan)
    #expect(BridgeSettings.imported(configJSON: Data("not json".utf8)) == nil)
}

@Test func savedSettingsSurviveNewFields() throws {
    // What an older build saved: only some keys.
    let s = try JSONDecoder().decode(BridgeSettings.self, from: Data(#"{"extraCards": true}"#.utf8))
    var want = BridgeSettings()
    want.extraCards = true
    #expect(s == want)
    var full = BridgeSettings()
    full.source = .admin
    full.refreshSec = 60
    full.lan = false
    full.gmailUser = "x"
    #expect(try JSONDecoder().decode(BridgeSettings.self, from: JSONEncoder().encode(full)) == full)
}

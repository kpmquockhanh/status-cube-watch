import Foundation
import Testing
@testable import CubeLinkCore

private let sample = Data(#"{"v":1,"bl":100,"sl":0,"rt":30,"pi":10,"pf":50,"ps":10,"pl":20,"pn":3,"ssid":"home","bridge":"http://h:8787/api/status","wifiPass":true,"otaPass":false}"#.utf8)

@Test func parsesWhatTheCubeReports() throws {
    let s = try #require(CubeSettings.parse(sample))
    #expect(s.backlight == 100 && s.sleepMin == 0 && s.rotateSec == 30 && s.pollSec == 10)
    #expect(s.focusMin == 50 && s.shortMin == 10 && s.longMin == 20 && s.sessions == 3)
    #expect(s.ssid == "home" && s.bridge == "http://h:8787/api/status")
    #expect(s.wifiPassSet && !s.otaPassSet)
    #expect(CubeSettings.parse(Data("nope".utf8)) == nil)
}

@Test func patchHoldsOnlyWhatChanged() throws {
    let old = try #require(CubeSettings.parse(sample))
    var new = old
    #expect(new.patch(from: old) == nil)
    new.backlight = 200
    new.focusMin = 45
    let json = try #require(new.patch(from: old))
    #expect(String(decoding: json, as: UTF8.self) == #"{"bl":200,"pf":45}"#)
    #expect(!CubeSettings.needsReboot(json))
}

@Test func networkChangesNeedAReboot() throws {
    let old = try #require(CubeSettings.parse(sample))
    var new = old
    new.ssid = "office"
    let json = try #require(new.patch(from: old, wifiPass: "hunter22hunter"))
    #expect(String(decoding: json, as: UTF8.self) == #"{"pass":"hunter22hunter","ssid":"office"}"#)
    #expect(CubeSettings.needsReboot(json))
    #expect(old.patch(from: old, otaPass: "x").map(CubeSettings.needsReboot) == true)
}

@Test func validityMatchesTheCubesRanges() {
    var s = CubeSettings()
    #expect(s.isValid)
    s.backlight = 9
    #expect(!s.isValid)
    s = CubeSettings(); s.pollSec = 61
    #expect(!s.isValid)
    s = CubeSettings(); s.sessions = 0
    #expect(!s.isValid)
}

@Test func aNewNetworkWithoutAPasswordIsAnOpenOne() throws {
    let old = try #require(CubeSettings.parse(sample))
    var new = old
    #expect(!new.joinsOpenNetwork(from: old, wifiPass: nil))  // same network: the stored password stays
    new.ssid = "office"
    #expect(new.joinsOpenNetwork(from: old, wifiPass: nil))
    #expect(new.joinsOpenNetwork(from: old, wifiPass: ""))
    #expect(!new.joinsOpenNetwork(from: old, wifiPass: "hunter22hunter"))
    new.ssid = ""
    #expect(!new.joinsOpenNetwork(from: old, wifiPass: nil))  // no network at all is not an open one
}

@Test func rebaseKeepsEditsAndFollowsTheCubeElsewhere() throws {
    let shown = try #require(CubeSettings.parse(sample))
    var form = shown
    form.focusMin = 45          // being edited
    form.ssid = "office"        // being edited
    var fresh = shown
    fresh.backlight = 200       // changed on the cube's own screen
    fresh.focusMin = 30         // also changed there, but the user's edit wins
    fresh.wifiPassSet = false
    let out = CubeSettings.rebase(form: form, shown: shown, onto: fresh)
    #expect(out.backlight == 200 && out.focusMin == 45 && out.ssid == "office")
    #expect(out.sleepMin == fresh.sleepMin && out.bridge == fresh.bridge)
    #expect(!out.wifiPassSet)   // flags always come from the cube
    #expect(CubeSettings.rebase(form: form, shown: nil, onto: fresh) == fresh)  // nothing shown yet
    #expect(CubeSettings.rebase(form: shown, shown: shown, onto: fresh) == fresh)  // no edits
}

@Test func soundExistsOnlyWhenTheCubeReportsIt() throws {
    let old = try #require(CubeSettings.parse(sample))  // no "sd": a cube before fw_rev 4
    #expect(old.sound == nil)
    var new = old
    new.backlight = 120
    let json = try #require(new.patch(from: old))
    #expect(String(decoding: json, as: UTF8.self) == #"{"bl":120}"#)  // never an "sd" it would ignore

    let cube = try #require(CubeSettings.parse(Data(#"{"v":1,"bl":100,"sd":2}"#.utf8)))
    #expect(cube.sound == 2)
    var edited = cube
    #expect(edited.patch(from: cube) == nil)
    edited.sound = 0
    let mute = try #require(edited.patch(from: cube))
    #expect(String(decoding: mute, as: UTF8.self) == #"{"sd":0}"#)
    #expect(!CubeSettings.needsReboot(mute))
    #expect(CubeSettings.parse(Data(#"{"sd":7}"#.utf8))?.sound == nil)  // out of range: as if absent
}

@Test func soundValidityAndRebase() throws {
    var s = CubeSettings()
    #expect(s.isValid)  // nil: nothing to check
    s.sound = 3
    #expect(s.isValid)
    s.sound = 4
    #expect(!s.isValid)

    let shown = try #require(CubeSettings.parse(Data(#"{"sd":2}"#.utf8)))
    var form = shown
    form.sound = 3  // being edited
    var fresh = shown
    fresh.sound = 1  // changed on the cube's own panel meanwhile
    #expect(CubeSettings.rebase(form: form, shown: shown, onto: fresh).sound == 3)
    #expect(CubeSettings.rebase(form: shown, shown: shown, onto: fresh).sound == 1)
}

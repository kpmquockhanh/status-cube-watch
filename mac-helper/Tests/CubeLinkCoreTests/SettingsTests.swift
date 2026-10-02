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

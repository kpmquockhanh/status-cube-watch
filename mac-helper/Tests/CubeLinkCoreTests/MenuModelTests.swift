import Foundation
import Testing
@testable import CubeLinkCore

private func payload(_ g1: Int?, _ g2: Int? = 10) -> Data {
    func card(_ t: String, _ g: Int?) -> String {
        "{\"t\":\"\(t)\",\"v\":\"2h 10m\",\"s1\":\"left\",\"s2\":\"used\"" + (g.map { ",\"g\":\($0)" } ?? "") + "}"
    }
    return Data("{\"v\":1,\"cards\":[\(card("Session", g1)),\(card("Week", g2))]}".utf8)
}

@Test func barShowsFirstRingPercentWithThresholdTints() {
    #expect(MenuModel.make(payload: payload(42), bridgeUp: true, link: .connected).barTitle == "42%")
    #expect(MenuModel.make(payload: payload(42), bridgeUp: true, link: .connected).tint == .normal)
    #expect(MenuModel.make(payload: payload(60), bridgeUp: true, link: .connected).tint == .amber)
    #expect(MenuModel.make(payload: payload(85), bridgeUp: true, link: .connected).tint == .red)
}

@Test func emptyTrackOrNoPayloadShowsDash() {
    #expect(MenuModel.make(payload: payload(-1), bridgeUp: true, link: .connected).barTitle == "–")
    #expect(MenuModel.make(payload: nil, bridgeUp: false, link: .searching).barTitle == "–")
    #expect(MenuModel.make(payload: Data("nope".utf8), bridgeUp: true, link: .connected).cards.isEmpty)
}

@Test func rowsAndStatusLines() {
    let m = MenuModel.make(payload: payload(42), bridgeUp: false, link: .bluetoothOff)
    #expect(m.cards.count == 2)
    #expect(m.cards[0] == CardRow(title: "Session  42%", detail: "2h 10m · left · used"))
    #expect(m.bridgeLine == "Bridge: down")
    #expect(m.cubeLine == "Cube: Bluetooth is off")
    #expect(!m.cubeConnected)
}

@Test func textCardHasNoPercent() {
    let m = MenuModel.make(payload: payload(nil, nil), bridgeUp: true, link: .connected)
    #expect(m.barTitle == "–")
    #expect(m.cards[0].title == "Session")
}

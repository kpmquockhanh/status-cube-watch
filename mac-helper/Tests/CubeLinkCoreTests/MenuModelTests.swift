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

@Test func usageCardListsBothWindowsAndMail() {
    let json = """
    {"v":1,"cards":[{"t":"CLAUDE","v":"4h 09m","s1":"to reset","s2":"","c":"green","g":14,"c2":"green","g2":53,
      "rows":[{"k":"5H","p":"14%","r":"4h 09m"},{"k":"7D","p":"53%","r":"1d 1h"}],"m":"12","mc":"amber"}]}
    """
    let m = MenuModel.make(payload: Data(json.utf8), bridgeUp: true, link: .connected)
    #expect(m.barTitle == "14%")  // the outer (5h) ring
    #expect(m.cards == [
        CardRow(title: "5H  14%", detail: "4h 09m"),
        CardRow(title: "7D  53%", detail: "1d 1h"),
        CardRow(title: "Unread mail", detail: "12"),
    ])
}

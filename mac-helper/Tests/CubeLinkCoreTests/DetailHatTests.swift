import Foundation
import Testing
@testable import CubeLinkCore

private func hex(_ s: String) -> Data { Data(s.split(separator: " ").map { UInt8($0, radix: 16)! }) }

@Test func parsesDetail() {
    #expect(ControlMessage.parse(hex("0a 00")) == .detail(agent: nil))
    #expect(ControlMessage.parse(hex("0a 06 6c 31 61 32 62 33")) == .detail(agent: "l1a2b3"))
    #expect(ControlMessage.parse(hex("0a 10 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61 61")) == nil)
    #expect(ControlMessage.parse(hex("0a")) == nil)
    #expect(ControlMessage.parse(hex("0a 03 61 62")) == nil)           // short
    #expect(ControlMessage.parse(hex("0a 01 61 62")) == nil)           // trailing byte
    #expect(ControlMessage.parse(hex("0a 01 07")) == nil)              // not printable
}

@Test func parsesHat() {
    #expect(ControlMessage.parse(hex("0b 06 6c 31 61 32 62 33 11")) == .hat(agent: "l1a2b3", hat: .code(17)))
    #expect(ControlMessage.parse(hex("0b 06 6c 31 61 32 62 33 00")) == .hat(agent: "l1a2b3", hat: .code(0)))
    #expect(ControlMessage.parse(hex("0b 06 6c 31 61 32 62 33 67")) == .hat(agent: "l1a2b3", hat: .code(103)))
    #expect(ControlMessage.parse(hex("0b 06 6c 31 61 32 62 33 fe")) == .hat(agent: "l1a2b3", hat: HatChoice.none))
    #expect(ControlMessage.parse(hex("0b 06 6c 31 61 32 62 33 ff")) == .hat(agent: "l1a2b3", hat: .auto))
    #expect(ControlMessage.parse(hex("0b 06 6c 31 61 32 62 33 68")) == nil)
    #expect(ControlMessage.parse(hex("0b 00 11")) == nil)
    #expect(ControlMessage.parse(hex("0b 06 6c 31 61 32 62 33")) == nil)
}

@Test func detailPollsEveryFastSecond() {
    #expect(PollPolicy.interval(payload: nil, detailOpen: true) == 1)
    #expect(PollPolicy.interval(payload: nil, detailOpen: false) == 5)
}

@Test func statusURLEncodesDetail() {
    #expect(BridgeClient.statusURL(port: 8787, detail: nil).absoluteString == "http://127.0.0.1:8787/api/status")
    #expect(BridgeClient.statusURL(port: 8787, detail: "a b").absoluteString == "http://127.0.0.1:8787/api/status?detail=a%20b")
}

@Test func hatRequestBody() throws {
    func body(_ h: HatChoice) throws -> [String: Any] {
        let r = BridgeClient.hatRequest(port: 8787, agent: "l1", hat: h)
        #expect(r.httpMethod == "POST")
        #expect(r.url?.absoluteString == "http://127.0.0.1:8787/api/hat")
        #expect(r.value(forHTTPHeaderField: "X-Cube-Client") == "1")
        #expect(r.timeoutInterval == 5)
        return try JSONSerialization.jsonObject(with: r.httpBody!) as! [String: Any]
    }
    let c = try body(.code(17))
    #expect(c["agent"] as? String == "l1" && c["hat"] as? Int == 17)
    #expect(try body(.none)["hat"] as? Int == -1)
    #expect(try body(.auto)["hat"] is NSNull)
}

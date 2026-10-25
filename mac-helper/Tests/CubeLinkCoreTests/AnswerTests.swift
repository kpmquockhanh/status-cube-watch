import Foundation
import Testing
@testable import CubeLinkCore

private func answerFixture(_ kind: String) throws -> [Substring] {
    var root = URL(fileURLWithPath: #filePath)
    for _ in 0..<4 { root.deleteLastPathComponent() }
    let text = try String(contentsOf: root.appendingPathComponent("firmware/sim/fixtures/ble-frames.txt"), encoding: .utf8)
    return text.split(separator: "\n").filter { $0.hasPrefix(kind + " ") }.map { $0.dropFirst(kind.count + 1) }
}

@Test func parsesFixtureAnswers() throws {
    let lines = try answerFixture("answer")
    #expect(lines.count == 2)
    for line in lines {
        let f = line.split(separator: " ")
        let bytes = f.dropFirst(3).map { UInt8($0, radix: 16)! }
        #expect(ControlMessage.parse(Data(bytes)) == .answer(agent: String(f[0]), prompt: String(f[1]), key: Character(String(f[2]))))
    }
}

@Test func rejectsMalformedAnswers() {
    func msg(_ la: UInt8, _ agent: String, _ lp: UInt8, _ prompt: String, _ key: UInt8) -> Data {
        Data([0x09, la]) + Data(agent.utf8) + Data([lp]) + Data(prompt.utf8) + Data([key])
    }
    #expect(ControlMessage.parse(msg(1, "a", 1, "p", 0x32)) == .answer(agent: "a", prompt: "p", key: "2"))
    #expect(ControlMessage.parse(Data([0x09])) == nil)
    #expect(ControlMessage.parse(msg(0, "", 1, "p", 0x32)) == nil)
    #expect(ControlMessage.parse(msg(16, "aaaaaaaaaaaaaaaa", 1, "p", 0x32)) == nil)
    #expect(ControlMessage.parse(msg(5, "a", 1, "p", 0x32)) == nil)                 // length past the end
    #expect(ControlMessage.parse(msg(1, "a", 1, "p", 0x32) + Data([0x00])) == nil)  // trailing byte
    #expect(ControlMessage.parse(msg(1, "a", 1, "p", 0x30)) == nil)                 // key '0'
    #expect(ControlMessage.parse(msg(1, "a", 1, "p", 0x61)) == nil)                 // key 'a'
    #expect(ControlMessage.parse(Data([0x09, 1, 0x7F, 1, 0x70, 0x32])) == nil)
    #expect(ControlMessage.parse(Data([0x09, 1, 0x61, 1, 0x19, 0x32])) == nil)
    #expect(ControlMessage.parse(Data([0x09, 1, 0x61, 0, 0x32])) == nil)            // lp 0
}

@Test func encodesScreenLocked() {
    let a = MacVolume(level: 56, muted: false, canSet: true, canMute: true, name: "Speakers", screenLocked: true)
    #expect(encodeVolume(a)[2] == 0x46)
    let b = MacVolume(level: 56, muted: false, canSet: true, canMute: true, name: "Speakers", locked: true, screenLocked: true)
    #expect(encodeVolume(b)[2] == 0x66)
    #expect(encodeVolume(MacVolume.noDevice)[2] == 0)
}

@Test func lockedAnswerIsDropped() {
    var p = AnswerPolicy()
    #expect(p.decide(locked: true) == .ignore("the Mac is locked"))
    #expect(p.decide(locked: false) == .post)  // the locked drop marked nothing in flight
}

@Test func secondAnswerWhileInFlightIsDropped() {
    var p = AnswerPolicy()
    #expect(p.decide(locked: false) == .post)
    #expect(p.decide(locked: false) == .ignore("an answer is still being sent"))
    p.finished()
    #expect(p.decide(locked: false) == .post)
}

@Test func answerRequestShape() throws {
    let r = BridgeClient.answerRequest(port: 8799, agent: "a1", prompt: "p7", key: "2")
    #expect(r.url?.absoluteString == "http://127.0.0.1:8799/api/answer")
    #expect(r.httpMethod == "POST")
    #expect(r.value(forHTTPHeaderField: "Content-Type") == "application/json")
    #expect(r.value(forHTTPHeaderField: "X-Cube-Client") == "1")
    #expect(r.value(forHTTPHeaderField: "Origin") == nil)
    #expect(r.timeoutInterval == 5)
    let body = try JSONSerialization.jsonObject(with: try #require(r.httpBody)) as? [String: String]
    #expect(body == ["agent": "a1", "prompt": "p7", "key": "2"])
}

@Test func outcomeWording() {
    #expect(answerOutcome(status: 204, error: nil) == .answered)
    #expect(answerOutcome(status: 409, error: nil) == .notNow)
    #expect(answerOutcome(status: 404, error: nil) == .trackingOff)
    #expect(answerOutcome(status: 500, error: nil) == .rejected(500))
    #expect(answerOutcome(status: nil, error: "offline") == .failed("offline"))
    #expect(describe(.answered) == "answered")
    #expect(describe(.notNow) == "not answerable now (409)")
    #expect(describe(.trackingOff).contains("agent tracking is off"))
    #expect(describe(.rejected(500)) == "refused by the bridge (HTTP 500)")
    #expect(describe(.failed("offline")) == "not sent: offline")
}

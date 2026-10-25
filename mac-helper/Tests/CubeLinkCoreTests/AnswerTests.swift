import Foundation
import Testing
@testable import CubeLinkCore

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

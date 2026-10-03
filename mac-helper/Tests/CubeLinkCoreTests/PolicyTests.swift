import Foundation
import Testing
@testable import CubeLinkCore

@Test func pushesFirstBodyThenOnlyOnChangeOrHeartbeat() {
    var p = PushPolicy(heartbeat: 5)
    let t0 = Date(timeIntervalSince1970: 1000)
    let a = Data("a".utf8), b = Data("b".utf8)

    let r1 = p.shouldSend(body: a, now: t0, force: false)
    #expect(r1)
    p.didSend(body: a, at: t0)
    let r2 = p.shouldSend(body: a, now: t0.addingTimeInterval(4.0), force: false)
    #expect(!r2)   // unchanged, no heartbeat yet
    let r3 = p.shouldSend(body: a, now: t0.addingTimeInterval(5), force: false)
    #expect(r3)      // heartbeat
    let r4 = p.shouldSend(body: b, now: t0.addingTimeInterval(1), force: false)
    #expect(r4)      // changed
}

@Test func forceSendsUnchangedBody() {
    var p = PushPolicy(heartbeat: 5)
    let t0 = Date(timeIntervalSince1970: 1000)
    let a = Data("a".utf8)
    p.didSend(body: a, at: t0)
    let r5 = p.shouldSend(body: a, now: t0.addingTimeInterval(1), force: true)
    #expect(r5)   // the cube asked "send now"
}

@Test func neverSendsWhenBridgeIsDown() {
    var p = PushPolicy(heartbeat: 5)
    let t0 = Date(timeIntervalSince1970: 1000)
    let r6 = p.shouldSend(body: nil, now: t0, force: true)
    #expect(!r6)                       // not even on "send now"
    p.didSend(body: Data("a".utf8), at: t0)
    let r7 = p.shouldSend(body: nil, now: t0.addingTimeInterval(60), force: false)
    #expect(!r7)  // so the cube sees honest staleness
}

@Test func backoffDoublesToThirtyAndResets() {
    var b = Backoff()
    let r8 = b.next()
    #expect(r8 == 1)
    let r9 = b.next()
    #expect(r9 == 2)
    let r10 = b.next()
    #expect(r10 == 4)
    for _ in 0..<10 { _ = b.next() }
    let r11 = b.next()
    #expect(r11 == 30)
    b.reset()
    let r12 = b.next()
    #expect(r12 == 1)
}

@Test func heartbeatToleratesTimerJitter() {
    var p = PushPolicy(heartbeat: 5)
    let t0 = Date(timeIntervalSince1970: 1000)
    let a = Data("a".utf8)
    p.didSend(body: a, at: t0)
    let early = p.shouldSend(body: a, now: t0.addingTimeInterval(4.7), force: false)
    #expect(early)    // a 5 s tick landing 0.3 s early must not slip to the next tick
    let tooSoon = p.shouldSend(body: a, now: t0.addingTimeInterval(2), force: false)
    #expect(!tooSoon)
}

@Test func askingDoesNotChangeThePolicy() {
    let p = PushPolicy(heartbeat: 5)  // a `let`: shouldSend is not mutating
    let t0 = Date(timeIntervalSince1970: 1000)
    #expect(p.shouldSend(body: Data("a".utf8), now: t0, force: false))
    #expect(p.shouldSend(body: Data("a".utf8), now: t0, force: false))  // still nothing sent
}

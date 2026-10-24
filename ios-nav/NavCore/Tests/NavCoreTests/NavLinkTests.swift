import Foundation
import Testing
@testable import NavCore

private func drain(_ q: inout NavChunkQueue, max n: Int = 1000) -> [Data] {
    var out: [Data] = []
    while out.count < n, let c = q.next() { out.append(c) }
    return out
}

private func frame(_ n: Int, fill: UInt8) -> Data { Data(repeating: fill, count: n) }

@Suite struct NavLinkTests {
    @Test func infoCheck() {
        #expect(CubeBLE.infoSupportsNav([1, 8]))
        #expect(CubeBLE.infoSupportsNav([1, 9]))
        #expect(!CubeBLE.infoSupportsNav([1, 7]))
        #expect(!CubeBLE.infoSupportsNav([2, 8]))
        #expect(!CubeBLE.infoSupportsNav([1]))
        #expect(!CubeBLE.infoSupportsNav([]))
    }

    @Test func panMessage() {
        #expect(CubeBLE.parsePan([0x08, 0xD8, 0xFF, 0x2C, 0x01]) == ScreenPoint(x: -40, y: 300))
        #expect(CubeBLE.parsePan([0x08, 0, 0, 0, 0]) == .zero)
        #expect(CubeBLE.parsePan([0x08, 0xE8, 0x03, 0x18, 0xFC]) == ScreenPoint(x: 1000, y: -1000))
        #expect(CubeBLE.parsePan([0x08, 0xE9, 0x03, 0, 0]) == nil)   // 1001: beyond the limit
        #expect(CubeBLE.parsePan([0x08, 0, 0, 0]) == nil)
        #expect(CubeBLE.parsePan([0x05, 40, 0, 0, 0]) == nil)        // the Mac's volume request
        #expect(CubeBLE.parsePan([]) == nil)
    }

    @Test func endedSceneEncodes() {
        #expect([UInt8](encode(.ended)) == [1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
    }

    @Test func singleFrameGoesOutInOrderWithSeqOne() {
        var q = NavChunkQueue()
        #expect(q.isIdle)
        #expect(q.offer(frame(10, fill: 7), maxWrite: 8) == .started)
        let out = drain(&q)
        #expect(out.count == 3)  // 4 body bytes per chunk
        #expect(out.map { [UInt8]($0.prefix(4)) } == [[1, 1, 0, 3], [1, 1, 1, 3], [1, 1, 2, 3]])
        #expect(q.isIdle)
        #expect(q.next() == nil)
    }

    @Test func newerFrameReplacesWaitingOneNeverInterleaves() {
        var q = NavChunkQueue()
        #expect(q.offer(frame(8, fill: 1), maxWrite: 8) == .started)  // seq 1, 2 chunks
        let first = q.next()!
        #expect(first[1] == 1 && first[2] == 0)
        #expect(q.offer(frame(4, fill: 2), maxWrite: 8) == .queued)    // replaced below, never sent
        #expect(q.offer(frame(4, fill: 3), maxWrite: 8) == .queued)
        let rest = drain(&q)
        #expect(rest.count == 2)
        // The current frame finishes whole, then the newest waiting frame with the next seq.
        #expect([UInt8](rest[0]) == [1, 1, 1, 2, 1, 1, 1, 1])
        #expect([UInt8](rest[1]) == [1, 2, 0, 1, 3, 3, 3, 3])
        #expect(q.isIdle)
    }

    @Test func idleQueueStartsNextFrameWithNewSeq() {
        var q = NavChunkQueue()
        _ = q.offer(frame(2, fill: 1), maxWrite: 20)
        _ = drain(&q)
        #expect(q.offer(frame(2, fill: 2), maxWrite: 20) == .started)
        #expect(q.next()![1] == 2)
    }

    @Test func seqWraps() {
        var q = NavChunkQueue()
        for _ in 0..<255 { _ = q.offer(frame(1, fill: 0), maxWrite: 20); _ = drain(&q) }
        #expect(q.seq == 255)
        _ = q.offer(frame(1, fill: 0), maxWrite: 20)
        #expect(q.next()![1] == 0)
    }

    @Test func tooLargeIsDropped() {
        var q = NavChunkQueue()
        // 32 chunks of 4 body bytes = 128 bytes fit; 129 does not.
        #expect(q.offer(frame(128, fill: 0), maxWrite: 8) == .started)
        #expect(drain(&q).count == 32)
        #expect(q.offer(frame(129, fill: 0), maxWrite: 8) == .tooLarge)
        #expect(q.isIdle)
        #expect(q.offer(frame(8193, fill: 0), maxWrite: 512) == .tooLarge)
        #expect(q.seq == 1)
    }

    @Test func resetClearsEverythingAndSeq() {
        var q = NavChunkQueue()
        _ = q.offer(frame(8, fill: 1), maxWrite: 8)
        _ = q.next()
        _ = q.offer(frame(8, fill: 2), maxWrite: 8)
        q.reset()
        #expect(q.isIdle)
        #expect(q.next() == nil)
        _ = q.offer(frame(1, fill: 0), maxWrite: 20)
        #expect(q.next()![1] == 1)
    }
}

@Suite struct NavLinkFixTests {
    @Test func parkedFrameNeverOvertakesNewerOne() {
        // A still has a chunk to send, B parks, A's last chunk goes out (B not started yet), C arrives.
        var r = NavChunkQueue()
        _ = r.offer(frame(8, fill: 1), maxWrite: 8)                     // A: 2 chunks, seq 1
        _ = r.next()
        #expect(r.offer(frame(4, fill: 2), maxWrite: 8) == .queued)    // B parks
        _ = r.next()                                                     // A's last chunk out
        #expect(r.offer(frame(4, fill: 3), maxWrite: 8) == .started)   // C starts, B is gone
        let out = drain(&r)
        #expect(out.count == 1)
        #expect([UInt8](out[0]) == [1, 2, 0, 1, 3, 3, 3, 3])
        #expect(r.isIdle)
    }

    @Test func dropPolicy() {
        typealias P = NavLinkPolicy
        #expect(P.onDrop(wasReady: true, remembered: true, tooOld: false, peerRemovedPairing: true) == .needsForget)
        #expect(P.onDrop(wasReady: false, remembered: true, tooOld: false, peerRemovedPairing: true) == .needsForget)
        #expect(P.onDrop(wasReady: true, remembered: true, tooOld: false, peerRemovedPairing: false) == .reconnectNow)
        #expect(P.onDrop(wasReady: true, remembered: false, tooOld: false, peerRemovedPairing: false) == .reconnectNow)
        #expect(P.onDrop(wasReady: false, remembered: true, tooOld: false, peerRemovedPairing: false) == .retryLater)
        #expect(P.onDrop(wasReady: false, remembered: false, tooOld: false, peerRemovedPairing: false) == .skipAndScan)
        #expect(P.onDrop(wasReady: false, remembered: false, tooOld: true, peerRemovedPairing: false) == .retryLater)
        #expect(P.onDrop(wasReady: false, remembered: true, tooOld: true, peerRemovedPairing: false) == .retryLater)
    }

    @Test func firstFrameOnALink() {
        let nav = NavScene(state: .navigating, turn: .left, exit: 0, dir: 0, gpsWeak: false,
                           dist: "1 km", street: "X", eta: "5 min", route: [], streets: [])
        typealias P = NavLinkPolicy
        #expect(P.firstFrame(firstPairing: true, last: nav, age: 0) == .ended)
        #expect(P.firstFrame(firstPairing: false, last: nil, age: 0) == .ended)
        #expect(P.firstFrame(firstPairing: false, last: nav, age: 1.9) == nav)
        #expect(P.firstFrame(firstPairing: false, last: nav, age: 2.5) == .ended)
    }
}

@Suite struct NavSkipListTests {
    @Test func entriesExpireAfterSixtySeconds() {
        var s = NavSkipList()
        let a = UUID(), b = UUID()
        let t0 = Date(timeIntervalSince1970: 1000)
        #expect(!s.contains(a, now: t0))
        s.skip(a, at: t0)
        #expect(s.contains(a, now: t0))
        #expect(s.contains(a, now: t0 + 59.9))
        #expect(!s.contains(a, now: t0 + 60))
        #expect(!s.contains(b, now: t0))
        s.skip(a, at: t0 + 60)  // skipped again: a fresh 60 s
        #expect(s.contains(a, now: t0 + 100))
        s.removeAll()
        #expect(!s.contains(a, now: t0 + 100))
    }
}

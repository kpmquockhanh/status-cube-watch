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

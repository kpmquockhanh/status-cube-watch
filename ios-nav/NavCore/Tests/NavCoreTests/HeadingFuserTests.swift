import Testing
import Foundation
@testable import NavCore

private let t0 = Date(timeIntervalSince1970: 1_000_000)
private func at(_ s: TimeInterval) -> Date { t0.addingTimeInterval(s) }

@Test func noOffsetUntilThreeSamples() {
    var f = HeadingFuser()
    f.learn(course: 0, compass: 90, at: at(0))
    f.learn(course: 0, compass: 90, at: at(1))
    #expect(f.offset(at: at(1)) == nil)
    f.learn(course: 0, compass: 90, at: at(2))
    #expect(f.offset(at: at(2)).map { abs($0 - 270) < 1e-9 } == true)
    #expect(f.heading(compass: 180, at: at(2)).map { abs($0 - 90) < 1e-9 } == true)
}

@Test func offsetsAcrossNorthAverageCircularly() {
    var f = HeadingFuser()
    // Offsets 358, 2, 0: the mean is 0, not 120.
    f.learn(course: 358, compass: 0, at: at(0))
    f.learn(course: 2, compass: 0, at: at(1))
    f.learn(course: 0, compass: 0, at: at(2))
    let o = try! #require(f.offset(at: at(2)))
    #expect(o < 0.5 || o > 359.5)
}

@Test func jumpingOffsetsAreNotTrusted() {
    var f = HeadingFuser()
    f.learn(course: 0, compass: 0, at: at(0))
    f.learn(course: 0, compass: 0, at: at(1))
    f.learn(course: 0, compass: 50, at: at(2))   // the phone moved in the pocket
    #expect(f.offset(at: at(2)) == nil)
    // Once the jump leaves the 5 s window, the new steady offset is trusted.
    for s in 3...8 { f.learn(course: 0, compass: 50, at: at(TimeInterval(s))) }
    #expect(f.offset(at: at(8)).map { abs($0 - 310) < 1e-9 } == true)
}

@Test func learningStaysFrozenWhileStoppedThenIsForgotten() {
    var f = HeadingFuser()
    for s in 0...4 { f.learn(course: 10, compass: 0, at: at(TimeInterval(s))) }
    #expect(f.offset(at: at(60)) != nil)          // a red light
    #expect(f.offset(at: at(4 + 120)) != nil)
    #expect(f.offset(at: at(4 + 121)) == nil)     // two minutes stopped: maybe got off
}

import XCTest
@testable import CubeLinkCore

final class LatestWinsGateTests: XCTestCase {
    private func d(_ b: UInt8) -> Data { Data([b]) }

    func testFirstOfferSendsImmediately() {
        var g = LatestWinsGate()
        XCTAssertEqual(g.offer(d(1)), d(1))
    }

    func testOffersWhileInFlightKeepOnlyTheLatest() {
        var g = LatestWinsGate()
        _ = g.offer(d(1))
        XCTAssertNil(g.offer(d(2)))
        XCTAssertNil(g.offer(d(3)))
        XCTAssertEqual(g.completed(), d(3))
        XCTAssertNil(g.completed())
    }

    func testCompletedWithNothingPendingFreesTheGate() {
        var g = LatestWinsGate()
        _ = g.offer(d(1))
        XCTAssertNil(g.completed())
        XCTAssertEqual(g.offer(d(2)), d(2))
    }

    func testPendingSendKeepsTheGateBusy() {
        var g = LatestWinsGate()
        _ = g.offer(d(1))
        _ = g.offer(d(2))
        XCTAssertEqual(g.completed(), d(2))
        XCTAssertNil(g.offer(d(3)))
        XCTAssertEqual(g.completed(), d(3))
    }

    func testResetDropsPendingAndInFlight() {
        var g = LatestWinsGate()
        _ = g.offer(d(1))
        _ = g.offer(d(2))
        g.reset()
        XCTAssertNil(g.completed())
        XCTAssertEqual(g.offer(d(4)), d(4))
    }
}

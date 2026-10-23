import Testing
@testable import NavCore

@Suite struct FormattingTests {
    @Test func distance() {
        #expect(formatDistance(7) == "Now")
        #expect(formatDistance(154) == "150 m")
        #expect(formatDistance(995) == "1.0 km")
        #expect(formatDistance(1240) == "1.2 km")
    }
    @Test func eta() {
        #expect(formatETA(720) == "12 min")
        #expect(formatETA(3900) == "1 h 05 min")
    }
}

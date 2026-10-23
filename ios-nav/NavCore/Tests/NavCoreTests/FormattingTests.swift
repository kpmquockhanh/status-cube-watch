import Testing
@testable import NavCore

@Suite struct FormattingTests {
    @Test func distance() {
        #expect(formatDistance(7) == "Now")
        #expect(formatDistance(154) == "150 m")
        #expect(formatDistance(995) == "1.0 km")
        #expect(formatDistance(1240) == "1.2 km")
        #expect(formatDistance(30) == "Now")
        #expect(formatDistance(50) == "50 m")
        #expect(formatDistance(163) == "160 m")
        #expect(formatDistance(960) == "960 m")
        #expect(formatDistance(994) == "990 m")
    }
    @Test func eta() {
        #expect(formatETA(720) == "12 min")
        #expect(formatETA(3900) == "1 h 05 min")
    }
}

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

    @Test func facts() {
        #expect(routeFacts(RouteStats(turns: 5, lights: 3, mainRoadShare: 0.2)) == "5 turns · 3 lights · main roads 20%")
        #expect(routeFacts(RouteStats(turns: 1, lights: 1, mainRoadShare: 0.704)) == "1 turn · 1 light · main roads 70%")
        #expect(routeFacts(RouteStats(turns: 2, lights: nil, mainRoadShare: 0)) == "2 turns · main roads 0%")
        #expect(routeFacts(RouteStats(turns: 0, lights: 0, mainRoadShare: 1)) == "0 turns · 0 lights · main roads 100%")
    }
}

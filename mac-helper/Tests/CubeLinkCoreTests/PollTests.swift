import Foundation
import Testing
@testable import CubeLinkCore

private func pollFixture(_ name: String) throws -> Data {
    var root = URL(fileURLWithPath: #filePath)
    for _ in 0..<4 { root.deleteLastPathComponent() }
    return try Data(contentsOf: root.appendingPathComponent("firmware/sim/fixtures/\(name)"))
}

@Test func pollFastWhileWaiting() throws {
    #expect(PollPolicy.interval(payload: try pollFixture("agents-full.json")) == 1)
    #expect(PollPolicy.interval(payload: try pollFixture("agents-lan.json")) == 1)
    #expect(PollPolicy.interval(payload: try pollFixture("usage.json")) == 5)
    #expect(PollPolicy.interval(payload: nil) == 5)
    #expect(PollPolicy.interval(payload: Data("garbage".utf8)) == 5)
}

@Test func onlyAWaitTileOnAnAgentsCardSpeedsPolling() {
    func p(_ s: String) -> Data { Data(s.utf8) }
    #expect(!PollPolicy.agentWaiting(payload: p(#"{"cards":[{"t":"AGENTS","a":[{"i":"1","k":"run"}]}]}"#)))
    #expect(!PollPolicy.agentWaiting(payload: p(#"{"cards":[{"t":"AGENTS","a":[]}]}"#)))
    #expect(!PollPolicy.agentWaiting(payload: p(#"{"cards":[{"t":"X","k":"wait"}]}"#)))
    #expect(PollPolicy.agentWaiting(payload: p(#"{"cards":[{"t":"A"},{"t":"AGENTS","a":[{"k":"run"},{"k":"wait"}]}]}"#)))
}

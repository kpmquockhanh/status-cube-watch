import Foundation
import Testing
@testable import CubeLinkCore

struct FixtureCase {
    var name = ""
    var seq = 0
    var chunk = 0
    var json = ""
    var frames: [[UInt8]] = []
}

func loadFixture() throws -> [FixtureCase] {
    // .../mac-helper/Tests/CubeLinkCoreTests/FrameTests.swift -> repo root is 4 levels up
    var root = URL(fileURLWithPath: #filePath)
    for _ in 0..<4 { root.deleteLastPathComponent() }
    let text = try String(contentsOf: root.appendingPathComponent("firmware/sim/fixtures/ble-frames.txt"), encoding: .utf8)
    var cases: [FixtureCase] = []
    for line in text.split(separator: "\n", omittingEmptySubsequences: true) where !line.hasPrefix("#") {
        let parts = line.split(separator: " ", maxSplits: 1, omittingEmptySubsequences: false)
        let key = String(parts[0])
        let rest = parts.count > 1 ? String(parts[1]) : ""
        switch key {
        case "case": cases.append(FixtureCase(name: rest))
        case "seq": cases[cases.count - 1].seq = Int(rest)!
        case "chunk": cases[cases.count - 1].chunk = Int(rest)!
        case "json": cases[cases.count - 1].json = rest
        case "frame": cases[cases.count - 1].frames.append(rest.split(separator: " ").map { UInt8($0, radix: 16)! })
        default: break
        }
    }
    return cases
}

@Test func encodesTheGoldenFrames() throws {
    let cases = try loadFixture()
    #expect(cases.count >= 2)  // a missing fixture must fail, not pass vacuously
    for c in cases {
        let frames = try encodeFrames(payload: Data(c.json.utf8), seq: UInt8(c.seq), maxWrite: c.chunk + CubeProtocol.headerSize)
        #expect(frames.map { [UInt8]($0) } == c.frames, "case \(c.name)")
    }
}

@Test func rejectsOversize() {
    let big = Data(repeating: 0x78, count: CubeProtocol.maxPayload + 1)
    #expect(throws: FrameError.tooLarge(CubeProtocol.maxPayload + 1)) {
        try encodeFrames(payload: big, seq: 1, maxWrite: 500)
    }
}

@Test func acceptsExactlyMaxPayloadInSixteenChunks() throws {
    let data = Data(repeating: 0x79, count: CubeProtocol.maxPayload)
    let frames = try encodeFrames(payload: data, seq: 1, maxWrite: 128 + CubeProtocol.headerSize)
    #expect(frames.count == 16)
    #expect(frames.last!.count == 128 + CubeProtocol.headerSize)
}

@Test func rejectsMoreThanSixteenChunks() {
    let data = Data(repeating: 0x7a, count: 1000)
    // 1000 bytes at 10 bytes per write = 100 chunks
    #expect(throws: FrameError.tooManyChunks(100)) {
        try encodeFrames(payload: data, seq: 1, maxWrite: 10 + CubeProtocol.headerSize)
    }
}

@Test func rejectsEmptyAndTinyMTU() {
    #expect(throws: FrameError.empty) { try encodeFrames(payload: Data(), seq: 1, maxWrite: 100) }
    #expect(throws: FrameError.mtuTooSmall(4)) { try encodeFrames(payload: Data([1]), seq: 1, maxWrite: 4) }
}

@Test func parsesControlMessages() {
    #expect(ControlMessage.parse(Data([0x01])) == .sendNow)
    #expect(ControlMessage.parse(Data([0x02, 9])) == .ack(seq: 9))
    #expect(ControlMessage.parse(Data([0x02])) == nil)   // ack without a seq
    #expect(ControlMessage.parse(Data([0x03, 0])) == .settings(.ok))
    #expect(ControlMessage.parse(Data([0x03, 2])) == .settings(.okReboot))
    #expect(ControlMessage.parse(Data([0x03, 9])) == nil)   // unknown result
    #expect(ControlMessage.parse(Data([0x03])) == nil)
    #expect(ControlMessage.parse(Data([0x7f])) == nil)
    #expect(ControlMessage.parse(Data()) == nil)
}

@Test func validatesBridgeBodies() {
    #expect(validatePayloadBody(Data("{\"v\":1,\"cards\":[]}".utf8)) == nil)
    #expect(validatePayloadBody(Data()) != nil)                                   // empty
    #expect(validatePayloadBody(Data("<html>502 Bad Gateway</html>".utf8)) != nil)  // HTML from a proxy
    #expect(validatePayloadBody(Data("[1,2,3]".utf8)) != nil)                     // JSON, but not an object
    #expect(validatePayloadBody(Data("{\"v\":".utf8)) != nil)                     // truncated
    var huge = "{\"x\":\""
    huge += String(repeating: "a", count: 2100)
    huge += "\"}"
    #expect(validatePayloadBody(Data(huge.utf8)) != nil)                          // over 2 KB
}

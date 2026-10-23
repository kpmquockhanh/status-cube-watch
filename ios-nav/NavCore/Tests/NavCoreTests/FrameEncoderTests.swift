import Foundation
import Testing
@testable import NavCore

private func pt(_ x: Int, _ y: Int) -> ScreenPoint { ScreenPoint(x: Int16(x), y: Int16(y)) }

private func goldenFrames() throws -> [String: [UInt8]] {
    // .../ios-nav/NavCore/Tests/NavCoreTests/File.swift -> repo root is 5 levels up
    var root = URL(fileURLWithPath: #filePath)
    for _ in 0..<5 { root.deleteLastPathComponent() }
    let text = try String(contentsOf: root.appendingPathComponent("firmware/sim/fixtures/nav-frames.txt"), encoding: .utf8)
    var out: [String: [UInt8]] = [:]
    for line in text.split(separator: "\n") where line.hasPrefix("nav ") {
        let parts = line.split(separator: " ")
        out[String(parts[1])] = parts[2...].map { UInt8($0, radix: 16)! }
    }
    return out
}

@Suite struct FrameEncoderTests {
    @Test func goldenScenes() throws {
        let golden = try goldenFrames()
        #expect(golden.count == 3)
        let turn = NavScene(state: .navigating, turn: .right, exit: 0, dir: 0, gpsWeak: false,
                            dist: "150 m", street: "Lê Lợi", eta: "12 min",
                            route: [pt(120, 200), pt(120, 90), pt(200, 80)],
                            streets: [SceneStreet(cls: 2, points: [pt(0, 150), pt(240, 160)])])
        let arrived = NavScene(state: .arrived, turn: .arrive, exit: 0, dir: 0, gpsWeak: false,
                               dist: "", street: "Bến Thành", eta: "", route: [], streets: [])
        let off = NavScene(state: .offRouteNoSignal, turn: .rejoin, exit: 0, dir: 64, gpsWeak: true,
                           dist: "80 m", street: "", eta: "", route: [pt(-64, 304), pt(300, -64)], streets: [])
        #expect([UInt8](encode(turn)) == golden["turn"])
        #expect([UInt8](encode(arrived)) == golden["arrived"])
        #expect([UInt8](encode(off)) == golden["offroute"])
    }

    private func scene(street: String) -> NavScene {
        NavScene(state: .navigating, turn: .straight, exit: 0, dir: 0, gpsWeak: false,
                 dist: "", street: street, eta: "", route: [], streets: [])
    }

    @Test func nfdEqualsNfc() {
        #expect(encode(scene(street: "Le\u{0302} Lo\u{031B}\u{0323}i")) == encode(scene(street: "Lê Lợi")))
    }

    @Test func truncatesOnCharacterBoundary() {
        let data = [UInt8](encode(scene(street: String(repeating: "ê", count: 35)))) // 70 bytes
        let len = Int(data[6 + 1])   // after header (6) and dist length byte (0)
        #expect(len <= 63)
        #expect(len == 62)
        #expect(String(decoding: data[8..<(8 + len)], as: UTF8.self) == String(repeating: "ê", count: 31))
    }

    @Test func chunking() {
        let frame = Data((0..<1000).map { UInt8($0 & 0xff) })
        let cs = chunks(frame, seq: 7, maxWrite: 185)
        #expect(cs.count == Int((1000.0 / 181).rounded(.up)))
        for (i, c) in cs.enumerated() {
            #expect(c[0] == 1 && c[1] == 7 && Int(c[2]) == i && Int(c[3]) == cs.count)
            #expect(c.count <= 185)
        }
        #expect(cs.reduce(Data()) { $0 + $1.dropFirst(4) } == frame)
    }

    @Test func capacity() {
        #expect(chunkCapacity(maxWrite: 20) == 512)
        #expect(chunkCapacity(maxWrite: 185) == 5792)
        #expect(chunkCapacity(maxWrite: 512) == 8192)
    }
}

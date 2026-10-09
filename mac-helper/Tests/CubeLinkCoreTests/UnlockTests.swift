import Foundation
import Testing
@testable import CubeLinkCore

private func fixture(_ kind: String) throws -> [Substring] {
    var root = URL(fileURLWithPath: #filePath)
    for _ in 0..<4 { root.deleteLastPathComponent() }
    let text = try String(contentsOf: root.appendingPathComponent("firmware/sim/fixtures/ble-frames.txt"), encoding: .utf8)
    return text.split(separator: "\n").filter { $0.hasPrefix(kind + " ") }.map { $0.dropFirst(kind.count + 1) }
}

@Test func unlockMatchesTheFirmwareFixture() throws {
    let lines = try fixture("unlock")
    #expect(lines.count == 1)
    for line in lines {
        let bytes = line.split(separator: " ").map { UInt8($0, radix: 16)! }
        #expect(ControlMessage.parse(Data(bytes)) == .unlock)
    }
}

@Test func lockedRidesOnTheVolumeFrame() {
    let v = MacVolume(level: 56, muted: false, canSet: true, canMute: true, name: "Speakers", locked: true)
    #expect(encodeVolume(v)[2] == 0x26)
    #expect(encodeVolume(MacVolume.noDevice)[2] == 0)
}

@Test func offersOnlyWhenOnAndLocked() {
    #expect(UnlockPolicy.offer(enabled: true, locked: true))
    #expect(!UnlockPolicy.offer(enabled: false, locked: true))
    #expect(!UnlockPolicy.offer(enabled: true, locked: false))
}

@Test func typesOnlyWhenEverythingAgrees() {
    var p = UnlockPolicy(minGap: 5)
    let t = Date(timeIntervalSince1970: 1000)
    #expect(p.decide(enabled: false, locked: true, trusted: true, now: t) != .type)
    #expect(p.decide(enabled: true, locked: false, trusted: true, now: t) != .type)
    #expect(p.decide(enabled: true, locked: true, trusted: false, now: t) != .type)
    #expect(p.decide(enabled: true, locked: true, trusted: true, now: t) == .type)
}

@Test func typesAtMostOncePerGap() {
    var p = UnlockPolicy(minGap: 5)
    let t = Date(timeIntervalSince1970: 1000)
    #expect(p.decide(enabled: true, locked: true, trusted: true, now: t) == .type)
    #expect(p.decide(enabled: true, locked: true, trusted: true, now: t + 4.9) != .type)
    #expect(p.decide(enabled: true, locked: true, trusted: true, now: t + 5) == .type)
}

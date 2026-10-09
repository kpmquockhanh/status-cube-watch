import Foundation
import Testing
@testable import CubeLinkCore

/// The fixture lines of one kind, without the key. A missing fixture fails the count checks below.
private func volumeFixture(_ kind: String) throws -> [Substring] {
    var root = URL(fileURLWithPath: #filePath)
    for _ in 0..<4 { root.deleteLastPathComponent() }
    let text = try String(contentsOf: root.appendingPathComponent("firmware/sim/fixtures/ble-frames.txt"), encoding: .utf8)
    return text.split(separator: "\n").filter { $0.hasPrefix(kind + " ") }.map { $0.dropFirst(kind.count + 1) }
}

@Test func volumeRequestMatchesTheFirmwareFixture() throws {
    let lines = try volumeFixture("vol_request")
    for line in lines {
        let f = line.split(separator: " ")
        let bytes = f.dropFirst(2).map { UInt8($0, radix: 16)! }
        #expect(ControlMessage.parse(Data(bytes)) == .volumeRequest(level: UInt8(f[0])!, muted: f[1] == "1"))
    }
    #expect(lines.count == 3)
}

@Test func mediaKeyMatchesTheFirmwareFixture() throws {
    let lines = try volumeFixture("media")
    let keys: [MediaKey] = [.playPause, .next, .previous]
    for line in lines {
        let f = line.split(separator: " ")
        let bytes = f.dropFirst(1).map { UInt8($0, radix: 16)! }
        #expect(ControlMessage.parse(Data(bytes)) == .media(keys[Int(f[0])!]))
    }
    #expect(lines.count == 3)
    #expect(ControlMessage.parse(Data([0x06])) == nil)     // no key
    #expect(ControlMessage.parse(Data([0x06, 3])) == nil)  // unknown key
}

@Test func volumeStateMatchesTheFirmwareFixture() throws {
    let lines = try volumeFixture("vol_state")
    for line in lines {
        let q0 = line.firstIndex(of: "\"")!
        let q1 = line[line.index(after: q0)...].firstIndex(of: "\"")!
        let head = line[..<q0].split(separator: " ")
        let name = String(line[line.index(after: q0)..<q1])
        let bytes = line[line.index(after: q1)...].split(separator: " ").map { UInt8($0, radix: 16)! }
        let flags = UInt8(head[1], radix: 16)!
        let v = MacVolume(level: head[0] == "none" ? nil : UInt8(head[0])!,
                          muted: flags & 1 != 0, canSet: flags & 2 != 0, canMute: flags & 4 != 0, name: name,
                          playing: flags & 8 != 0 ? flags & 16 != 0 : nil)
        #expect(encodeVolume(v) == Data(bytes))
    }
    #expect(lines.count == 6)
}

@Test func parsesVolumeRequests() {
    #expect(ControlMessage.parse(Data([0x05, 100, 1])) == .volumeRequest(level: 100, muted: true))
    #expect(ControlMessage.parse(Data([0x05, 0, 0])) == .volumeRequest(level: 0, muted: false))
    #expect(ControlMessage.parse(Data([0x05])) == nil)           // no level
    #expect(ControlMessage.parse(Data([0x05, 0x10])) == nil)     // no muted byte
    #expect(ControlMessage.parse(Data([0x05, 101, 0])) == nil)   // level over 100
    #expect(ControlMessage.parse(Data([0x05, 50, 2])) == nil)    // muted is 0 or 1
}

// Review Focus 5: the cube's fonts are printable ASCII and it rejects anything
// else, so the name must be folded here.
@Test func foldsDeviceNamesForTheCube() {
    #expect(foldVolumeName("Café’s Speakers") == "Cafe's Speakers")
    #expect(foldVolumeName("🎧 Studio") == "Studio")
    #expect(foldVolumeName("A Very Long Output Device Name") == "A Very Long Output Devi")
    #expect(foldVolumeName("") == "")
    let d = encodeVolume(MacVolume(level: 150, muted: false, canSet: true, canMute: true,
                                   name: "Ünïcödé — Output Device Long"))
    #expect(d[1] == 100)                                        // level clamped
    #expect(d.count <= 26)
    #expect(d.dropFirst(3).allSatisfy { $0 >= 0x20 && $0 <= 0x7E })
    #expect(encodeVolume(.noDevice) == Data([1, 0xFF, 0]))
}

import Foundation

/// Wire constants. Mirrors firmware/src/ble_frame.h and docs/ble-protocol.md;
/// firmware/sim/fixtures/ble-frames.txt pins both sides.
public enum CubeProtocol {
    public static let version: UInt8 = 1
    public static let headerSize = 4
    public static let maxChunks = 16
    public static let maxPayload = 2048

    public static let serviceUUID = "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A01"
    public static let payloadUUID = "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A02"
    public static let controlUUID = "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A03"
    public static let infoUUID = "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A04"
    public static let settingsUUID = "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A05"
    public static let volumeUUID = "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A06"
    public static let maxSettings = 512  // one ATT value
}

public enum FrameError: Error, Equatable {
    case empty
    case tooLarge(Int)
    case mtuTooSmall(Int)
    case tooManyChunks(Int)
}

/// Splits a payload into frames `[ver, seq, idx, total] + bytes`, each at most
/// `maxWrite` bytes (the peripheral's maximumWriteValueLength).
public func encodeFrames(payload: Data, seq: UInt8, maxWrite: Int) throws -> [Data] {
    guard !payload.isEmpty else { throw FrameError.empty }
    guard payload.count <= CubeProtocol.maxPayload else { throw FrameError.tooLarge(payload.count) }
    let chunk = maxWrite - CubeProtocol.headerSize
    guard chunk > 0 else { throw FrameError.mtuTooSmall(maxWrite) }
    let total = (payload.count + chunk - 1) / chunk
    guard total <= CubeProtocol.maxChunks else { throw FrameError.tooManyChunks(total) }

    var frames: [Data] = []
    for idx in 0..<total {
        let lo = payload.startIndex + idx * chunk
        let hi = min(lo + chunk, payload.endIndex)
        var f = Data([CubeProtocol.version, seq, UInt8(idx), UInt8(total)])
        f.append(payload[lo..<hi])
        frames.append(f)
    }
    return frames
}

/// A Pomodoro phase as the cube numbers it (firmware `PomoPhase`).
public enum PomodoroPhase: UInt8, Equatable {
    case focus = 0
    case shortBreak = 1
    case longBreak = 2
}

/// A media key the cube's Volume card asks the Mac to press (Control `06 <key>`, fw_rev 6).
public enum MediaKey: UInt8, Equatable {
    case playPause = 0
    case next = 1
    case previous = 2
}

/// What the cube sends on the Control characteristic.
public enum ControlMessage: Equatable {
    case sendNow
    case ack(seq: UInt8)
    case settings(SettingsResult)
    case pomodoroEnded(ended: PomodoroPhase, next: PomodoroPhase)
    /// Set the Mac's output: an absolute level 0...100 and mute (fw_rev 5).
    case volumeRequest(level: UInt8, muted: Bool)
    /// Press a media key on the Mac (fw_rev 6).
    case media(MediaKey)
    /// Unlock the Mac's screen: a tap on the cube's unlock prompt (fw_rev 7).
    case unlock

    public static func parse(_ d: Data) -> ControlMessage? {
        let b = [UInt8](d)
        guard let first = b.first else { return nil }
        switch first {
        case 0x01: return .sendNow
        case 0x02: return b.count >= 2 ? .ack(seq: b[1]) : nil
        case 0x03:
            guard b.count >= 2, let r = SettingsResult(rawValue: b[1]) else { return nil }
            return .settings(r)
        case 0x04:
            guard b.count >= 3, let e = PomodoroPhase(rawValue: b[1]), let n = PomodoroPhase(rawValue: b[2]) else { return nil }
            return .pomodoroEnded(ended: e, next: n)
        case 0x05:
            guard b.count >= 3, b[1] <= 100, b[2] <= 1 else { return nil }
            return .volumeRequest(level: b[1], muted: b[2] == 1)
        case 0x06:
            guard b.count >= 2, let k = MediaKey(rawValue: b[1]) else { return nil }
            return .media(k)
        case 0x07: return .unlock
        default: return nil
        }
    }
}

/// The cube's reply to a Settings write: Control `03 <result>`.
public enum SettingsResult: UInt8, Equatable {
    case ok = 0
    case invalid = 1
    case okReboot = 2  // applied; the network settings changed, so the cube is rebooting
}

/// Reconnect delays: 1 s, doubling, capped at 30 s.
public struct Backoff {
    private var current: TimeInterval = 1
    public init() {}
    public mutating func next() -> TimeInterval {
        let v = current
        current = min(current * 2, 30)
        return v
    }
    public mutating func reset() { current = 1 }
}

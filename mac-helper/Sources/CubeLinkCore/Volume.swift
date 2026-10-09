import Foundation

/// The Mac's default output as the cube's Volume card shows it. `level` nil = no output device.
public struct MacVolume: Equatable {
    public var level: UInt8?
    public var muted: Bool
    public var canSet: Bool
    public var canMute: Bool
    public var name: String
    /// Whether the Now Playing app is playing; nil = unknown (the cube then draws play/pause).
    public var playing: Bool?

    public init(level: UInt8?, muted: Bool, canSet: Bool, canMute: Bool, name: String, playing: Bool? = nil) {
        self.level = level
        self.muted = muted
        self.canSet = canSet
        self.canMute = canMute
        self.name = name
        self.playing = playing
    }

    public static let noDevice = MacVolume(level: nil, muted: false, canSet: false, canMute: false, name: "")
}

/// The Volume characteristic's layout: `[ver=1][level][flags][name...]` (docs/ble-protocol.md).
enum VolumeFrame {
    static let version: UInt8 = 1
    static let noDevice: UInt8 = 0xFF
    static let maxName = 23
}

/// The device name as the cube can draw it and will accept: printable ASCII,
/// diacritics stripped, anything else dropped, at most 23 bytes.
public func foldVolumeName(_ s: String) -> String {
    let folded = s.replacingOccurrences(of: "\u{2019}", with: "'")
        .folding(options: .diacriticInsensitive, locale: nil)
    var ascii = String.UnicodeScalarView()
    ascii.append(contentsOf: folded.unicodeScalars.filter { $0.value >= 0x20 && $0.value <= 0x7E })
    let trimmed = String(ascii).trimmingCharacters(in: .whitespaces)
    return String(trimmed.prefix(VolumeFrame.maxName)).trimmingCharacters(in: .whitespaces)
}

/// The value written to the cube's Volume characteristic.
public func encodeVolume(_ v: MacVolume) -> Data {
    var flags: UInt8 = 0
    if v.muted { flags |= 1 }
    if v.canSet { flags |= 2 }
    if v.canMute { flags |= 4 }
    if let playing = v.playing {  // bit 3: the state is known, bit 4: playing (fw_rev 6 draws it)
        flags |= 8
        if playing { flags |= 16 }
    }
    let level = v.level.map { min($0, 100) } ?? VolumeFrame.noDevice
    return Data([VolumeFrame.version, level, flags]) + Data(foldVolumeName(v.name).utf8)
}

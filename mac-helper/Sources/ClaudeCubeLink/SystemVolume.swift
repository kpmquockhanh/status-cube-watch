import AudioToolbox
import CoreAudio
import CubeLinkCore
import Foundation

/// The Mac's default output device: its volume, mute and name, and changes to any of them or
/// to which device is the default. Needs no permission prompt. Main queue only.
final class SystemVolume {
    /// Called on the main queue 30 ms after the last of a burst of changes.
    var onChange: ((MacVolume) -> Void)?

    private static let unknown = AudioObjectID(kAudioObjectUnknown)
    private static let deviceProps: [AudioObjectPropertyAddress] = [
        address(kAudioHardwareServiceDeviceProperty_VirtualMainVolume),
        address(kAudioDevicePropertyMute),
        address(kAudioObjectPropertyName, scope: kAudioObjectPropertyScopeGlobal),
    ]

    private var device = SystemVolume.unknown
    private var pending: DispatchWorkItem?
    // Stored as block types, so the same block object is passed to Add and Remove: Core Audio
    // matches listeners by block identity, and a Swift closure passed inline would be a new
    // block each time and never be removed.
    private lazy var changed: AudioObjectPropertyListenerBlock = { [weak self] _, _ in self?.schedule() }
    private lazy var outputChanged: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
        self?.follow()
        self?.schedule()
    }

    func start() {
        var a = Self.address(kAudioHardwarePropertyDefaultOutputDevice, scope: kAudioObjectPropertyScopeGlobal)
        let s = AudioObjectAddPropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &a, .main, outputChanged)
        if s != noErr { Trace.log("volume", "default-output listener failed: \(s)") }
        follow()
    }

    /// The default output as the cube shows it.
    func current() -> MacVolume {
        guard device != Self.unknown else { return .noDevice }
        let volAddr = Self.address(kAudioHardwareServiceDeviceProperty_VirtualMainVolume)
        let muteAddr = Self.address(kAudioDevicePropertyMute)
        let scalar: Float32? = read(volAddr)
        let mute: UInt32? = read(muteAddr)
        return MacVolume(
            level: UInt8((min(max(scalar ?? 1, 0), 1) * 100).rounded()),
            muted: (mute ?? 0) != 0,
            canSet: scalar != nil && settable(volAddr),
            canMute: mute != nil && settable(muteAddr),
            name: name())
    }

    /// Sets mute, then the volume (level / 100), each only where the device allows it.
    func apply(level: UInt8, muted: Bool) {
        guard device != Self.unknown else { return }
        var muteAddr = Self.address(kAudioDevicePropertyMute)
        if settable(muteAddr) {
            var m: UInt32 = muted ? 1 : 0
            let s = AudioObjectSetPropertyData(device, &muteAddr, 0, nil, UInt32(MemoryLayout<UInt32>.size), &m)
            if s != noErr { Trace.log("volume", "set mute failed: \(s)") }
        }
        var volAddr = Self.address(kAudioHardwareServiceDeviceProperty_VirtualMainVolume)
        if settable(volAddr) {
            var v = Float32(min(level, 100)) / 100
            let s = AudioObjectSetPropertyData(device, &volAddr, 0, nil, UInt32(MemoryLayout<Float32>.size), &v)
            if s != noErr { Trace.log("volume", "set volume failed: \(s)") }
        }
    }

    // MARK: helpers

    /// Moves the device listeners to the current default output.
    private func follow() {
        let next = Self.defaultOutput()
        guard next != device else { return }
        if device != Self.unknown {
            for prop in Self.deviceProps {
                var a = prop
                if AudioObjectHasProperty(device, &a) {
                    _ = AudioObjectRemovePropertyListenerBlock(device, &a, .main, changed)
                }
            }
        }
        device = next
        guard device != Self.unknown else { return }
        for prop in Self.deviceProps {
            var a = prop
            if AudioObjectHasProperty(device, &a) {
                _ = AudioObjectAddPropertyListenerBlock(device, &a, .main, changed)
            }
        }
    }

    private func schedule() {
        pending?.cancel()
        let w = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.onChange?(self.current())
        }
        pending = w
        DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(30), execute: w)
    }

    private static func address(_ sel: AudioObjectPropertySelector,
                                scope: AudioObjectPropertyScope = kAudioDevicePropertyScopeOutput) -> AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(mSelector: sel, mScope: scope, mElement: kAudioObjectPropertyElementMain)
    }

    private static func defaultOutput() -> AudioObjectID {
        var a = address(kAudioHardwarePropertyDefaultOutputDevice, scope: kAudioObjectPropertyScopeGlobal)
        var id = unknown
        var size = UInt32(MemoryLayout<AudioObjectID>.size)
        let s = AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &a, 0, nil, &size, &id)
        return s == noErr ? id : unknown
    }

    private func settable(_ addr: AudioObjectPropertyAddress) -> Bool {
        var a = addr
        guard AudioObjectHasProperty(device, &a) else { return false }
        var ok = DarwinBoolean(false)
        return AudioObjectIsPropertySettable(device, &a, &ok) == noErr && ok.boolValue
    }

    private func read<T>(_ addr: AudioObjectPropertyAddress) -> T? {
        var a = addr
        guard AudioObjectHasProperty(device, &a) else { return nil }
        var size = UInt32(MemoryLayout<T>.size)
        let p = UnsafeMutablePointer<T>.allocate(capacity: 1)
        defer { p.deallocate() }
        guard AudioObjectGetPropertyData(device, &a, 0, nil, &size, p) == noErr else { return nil }
        return p.pointee
    }

    private func name() -> String {
        var a = Self.address(kAudioObjectPropertyName, scope: kAudioObjectPropertyScopeGlobal)
        var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        var cf: Unmanaged<CFString>?
        guard AudioObjectGetPropertyData(device, &a, 0, nil, &size, &cf) == noErr, let n = cf else { return "" }
        return n.takeRetainedValue() as String
    }
}

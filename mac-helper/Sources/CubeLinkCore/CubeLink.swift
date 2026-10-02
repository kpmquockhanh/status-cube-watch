import CoreBluetooth
import Foundation

/// The Bluetooth side: find the cube, pair, then write payloads to it.
///
/// Flow: scan by service UUID (or reconnect by the stored identifier) ->
/// connect -> discover -> READ Info (encrypted, so this is what makes macOS ask
/// for the passkey) -> check the protocol version -> subscribe to Control ->
/// ready. The cube sends "send now" on subscribe and an ACK per payload.
public final class CubeLink: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    public var onSendNow: (() -> Void)?
    /// The cube's settings, read once the link is ready and again after every write. Nil while
    /// disconnected, or when the cube's firmware predates the Settings characteristic.
    public private(set) var cubeSettings: CubeSettings?
    public var onSettings: ((CubeSettings?) -> Void)?
    /// The cube's verdict on a settings write.
    public var onSettingsResult: ((SettingsResult) -> Void)?
    public private(set) var isReady = false
    public private(set) var state: LinkState = .searching {
        didSet { if state != oldValue { onStateChange?(state) } }
    }
    public var onStateChange: ((LinkState) -> Void)?

    private let log: (String) -> Void
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var payloadChar: CBCharacteristic?
    private var controlChar: CBCharacteristic?
    private var infoChar: CBCharacteristic?
    private var settingsChar: CBCharacteristic?
    private var backoff = Backoff()
    private var seq: UInt8 = 0
    private var awaitingAck: UInt8?
    private var ackTimeout: DispatchWorkItem?
    private var retried = false
    private var lastPayload: Data?

    private let serviceID = CBUUID(string: CubeProtocol.serviceUUID)
    private let payloadID = CBUUID(string: CubeProtocol.payloadUUID)
    private let controlID = CBUUID(string: CubeProtocol.controlUUID)
    private let infoID = CBUUID(string: CubeProtocol.infoUUID)
    private let settingsID = CBUUID(string: CubeProtocol.settingsUUID)
    private let idKey = "cubeIdentifier"

    public init(log: @escaping (String) -> Void) {
        self.log = log
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    // MARK: sending

    /// Writes `payload` to the cube. A no-op until the link is ready.
    public func send(_ payload: Data) {
        guard isReady, let p = peripheral, let payloadChar else {
            Trace.log("ble", "send skipped: link not ready (\(payload.count) B)")
            return
        }
        lastPayload = payload
        retried = false
        write(payload, to: p, char: payloadChar)
    }

    /// Writes a settings patch (see `CubeSettings.patch`). The cube answers on Control, then
    /// the new values are read back. False when there is nothing to write to.
    @discardableResult
    public func writeSettings(_ patch: Data) -> Bool {
        guard isReady, let p = peripheral, let ch = settingsChar, patch.count <= CubeProtocol.maxSettings else {
            Trace.log("ble", "settings write skipped (ready \(isReady), char \(settingsChar != nil), \(patch.count) B)")
            return false
        }
        Trace.log("ble", "settings write: \(String(decoding: patch, as: UTF8.self))")
        p.writeValue(patch, for: ch, type: .withResponse)
        return true
    }

    /// Asks the cube for its current settings again.
    public func refreshSettings() {
        guard isReady, let p = peripheral, let ch = settingsChar else { return }
        p.readValue(for: ch)
    }

    private func write(_ payload: Data, to p: CBPeripheral, char: CBCharacteristic) {
        seq &+= 1  // a retry uses a new seq, so the cube never mistakes it for a replay
        do {
            let frames = try encodeFrames(payload: payload, seq: seq,
                                          maxWrite: p.maximumWriteValueLength(for: .withResponse))
            Trace.log("ble", "send seq \(seq): \(payload.count) B in \(frames.count) frame(s), maxWrite \(p.maximumWriteValueLength(for: .withResponse))")
            for f in frames { p.writeValue(f, for: char, type: .withResponse) }
            awaitingAck = seq
            ackTimeout?.cancel()
            let item = DispatchWorkItem { [weak self] in self?.ackTimedOut() }
            ackTimeout = item
            DispatchQueue.main.asyncAfter(deadline: .now() + 2, execute: item)
        } catch {
            log("not sending: \(error)")
        }
    }

    private func ackTimedOut() {
        guard awaitingAck != nil, let p = peripheral, let c = payloadChar, let payload = lastPayload else { return }
        awaitingAck = nil
        Trace.log("ble", "ACK timeout (retried: \(retried))")
        if !retried {
            retried = true
            log("no ACK, retrying once")
            write(payload, to: p, char: c)
        } else {
            log("no ACK after retry; the next heartbeat will try again")
        }
    }

    // MARK: connecting

    public func centralManagerDidUpdateState(_ c: CBCentralManager) {
        Trace.log("ble", "central state \(c.state.rawValue)")
        switch c.state {
        case .poweredOn:
            log("Bluetooth on")
            state = .searching
            connect()
        case .unauthorized:
            log("Bluetooth permission denied: allow Claude Cube Link in System Settings > Privacy & Security > Bluetooth")
            setReady(false)
            state = .unauthorized
        default:
            log("Bluetooth unavailable (state \(c.state.rawValue))")
            setReady(false)
            state = .bluetoothOff
        }
    }

    private func connect() {
        guard central.state == .poweredOn else { return }
        if let s = UserDefaults.standard.string(forKey: idKey), let id = UUID(uuidString: s),
           let known = central.retrievePeripherals(withIdentifiers: [id]).first {
            log("reconnecting to the paired cube")
            peripheral = known
            central.connect(known)  // stays pending until the cube is in range
        } else {
            log("scanning for a cube (put it in pairing mode: no bond on the cube)")
            central.scanForPeripherals(withServices: [serviceID])
        }
    }

    public func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral,
                               advertisementData: [String: Any], rssi: NSNumber) {
        Trace.log("ble", "discovered \(p.identifier) rssi \(rssi) adv \(advertisementData.keys.sorted())")
        log("found \(p.name ?? "a cube")")
        c.stopScan()
        peripheral = p
        c.connect(p)
    }

    public func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        Trace.log("ble", "didConnect \(p.identifier)")
        log("connected")
        state = .connecting
        p.delegate = self
        p.discoverServices([serviceID])
    }

    public func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        explain(error)
        reconnectLater()
    }

    public func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        log("disconnected")
        explain(error)
        setReady(false)
        reconnectLater()
    }

    private func reconnectLater() {
        let delay = backoff.next()
        Trace.log("ble", "reconnect in \(delay) s")
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in self?.connect() }
    }

    // MARK: discovery

    public func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            explain(error)
            setupFailed(p)
            return
        }
        guard let svc = p.services?.first(where: { $0.uuid == serviceID }) else {
            log("cube service not found")
            setupFailed(p)
            return
        }
        p.discoverCharacteristics([payloadID, controlID, infoID, settingsID], for: svc)
    }

    public func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        if let error {
            explain(error)
            setupFailed(p)
            return
        }
        for ch in s.characteristics ?? [] {
            if ch.uuid == payloadID { payloadChar = ch }
            else if ch.uuid == controlID { controlChar = ch }
            else if ch.uuid == infoID { infoChar = ch }
            else if ch.uuid == settingsID { settingsChar = ch }  // optional: older firmware has none
        }
        guard let info = infoChar, controlChar != nil, payloadChar != nil else {
            log("cube is missing a characteristic")
            setupFailed(p)
            return
        }
        log("cube characteristics: \((s.characteristics ?? []).map { String($0.uuid.uuidString.suffix(2)) }.joined(separator: ","))"
            + (settingsChar == nil ? " (no Settings characteristic)" : ""))
        log("reading Info; if this is a new cube, macOS now asks for the code shown on its screen")
        p.readValue(for: info)
    }

    // MARK: values

    public func peripheral(_ p: CBPeripheral, didUpdateValueFor ch: CBCharacteristic, error: Error?) {
        if ch.uuid == infoID {
            if let error {
                explain(error)
                setupFailed(p)
                return
            }
            let b = [UInt8](ch.value ?? Data())
            Trace.log("ble", "Info read: \(b)")
            if b.count >= 2, b[1] >= 2, settingsChar == nil {
                log("cube firmware has Settings (rev \(b[1])) but macOS did not list the characteristic: "
                    + "it is serving a cached copy of the old GATT table")
            }
            guard b.first == CubeProtocol.version else {
                log("cube speaks protocol \(b.first.map(String.init) ?? "?"), this app speaks \(CubeProtocol.version); update one of them")
                setupFailed(p)
                return
            }
            UserDefaults.standard.set(p.identifier.uuidString, forKey: idKey)
            if let c = controlChar { p.setNotifyValue(true, for: c) }
        } else if ch.uuid == settingsID {
            if let error {
                Trace.log("ble", "settings read failed: \(error.localizedDescription)")
            } else if let value = ch.value {
                cubeSettings = CubeSettings.parse(value)
                Trace.log("ble", "settings read: \(cubeSettings != nil ? "ok" : "unparseable")")
                onSettings?(cubeSettings)
            }
        } else if ch.uuid == controlID, let value = ch.value, let msg = ControlMessage.parse(value) {
            Trace.log("ble", "control: \(msg)")
            switch msg {
            case .sendNow:
                onSendNow?()
            case .settings(let r):
                onSettingsResult?(r)
                if r != .okReboot { refreshSettings() }
            case .ack(let s):
                if awaitingAck == s {
                    awaitingAck = nil
                    ackTimeout?.cancel()
                }
            }
        }
    }

    public func peripheral(_ p: CBPeripheral, didUpdateNotificationStateFor ch: CBCharacteristic, error: Error?) {
        if let error {
            explain(error)
            setupFailed(p)
            return
        }
        if ch.uuid == controlID, ch.isNotifying {
            log("ready")
            backoff.reset()
            setReady(true)
            refreshSettings()
        }
    }

    public func peripheral(_ p: CBPeripheral, didWriteValueFor ch: CBCharacteristic, error: Error?) {
        Trace.log("ble", "didWrite \(ch.uuid) error: \(error?.localizedDescription ?? "none")")
        if let error { explain(error) }
    }

    // MARK: helpers

    /// A setup step failed while the link is still up. CoreBluetooth would keep it
    /// connected forever, so drop it: didDisconnect then backs off and retries.
    private func setupFailed(_ p: CBPeripheral) {
        setReady(false)
        central.cancelPeripheralConnection(p)
    }

    private func setReady(_ ready: Bool) {
        if ready != isReady { Trace.log("ble", "ready -> \(ready)") }
        isReady = ready
        if ready { state = .connected }
        else if state == .connected { state = .searching }
        if !ready {
            payloadChar = nil
            controlChar = nil
            infoChar = nil
            settingsChar = nil
            if cubeSettings != nil {
                cubeSettings = nil
                onSettings?(nil)
            }
            awaitingAck = nil
            ackTimeout?.cancel()
        }
    }

    /// Turns the Bluetooth errors a user can act on into a sentence.
    private func explain(_ error: Error?) {
        guard let error else { return }
        Trace.log("ble", "error: \(error) (\((error as NSError).domain) \((error as NSError).code))")
        if let e = error as? CBError, e.code == .encryptionTimedOut {
            log("pairing timed out or was cancelled; retrying the link")
        } else if let e = error as? CBError, e.code == .peerRemovedPairingInformation {
            log("the cube forgot this Mac (its flash was erased or the bond was forgotten). "
                + "Remove \"Claude Cube\" in System Settings > Bluetooth, then pair again.")
        } else if let e = error as? CBATTError,
                  e.code == .insufficientEncryption || e.code == .insufficientAuthentication {
            log("waiting for pairing: enter the code shown on the cube")
        } else {
            log("bluetooth error: \(error.localizedDescription)")
        }
    }
}

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
    public private(set) var isReady = false

    private let log: (String) -> Void
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var payloadChar: CBCharacteristic?
    private var controlChar: CBCharacteristic?
    private var infoChar: CBCharacteristic?
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
    private let idKey = "cubeIdentifier"

    public init(log: @escaping (String) -> Void) {
        self.log = log
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    // MARK: sending

    /// Writes `payload` to the cube. A no-op until the link is ready.
    public func send(_ payload: Data) {
        guard isReady, let p = peripheral, let payloadChar else { return }
        lastPayload = payload
        retried = false
        write(payload, to: p, char: payloadChar)
    }

    private func write(_ payload: Data, to p: CBPeripheral, char: CBCharacteristic) {
        seq &+= 1  // a retry uses a new seq, so the cube never mistakes it for a replay
        do {
            let frames = try encodeFrames(payload: payload, seq: seq,
                                          maxWrite: p.maximumWriteValueLength(for: .withResponse))
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
        switch c.state {
        case .poweredOn:
            log("Bluetooth on")
            connect()
        case .unauthorized:
            log("Bluetooth permission denied: allow Claude Cube Link in System Settings > Privacy & Security > Bluetooth")
            setReady(false)
        default:
            log("Bluetooth unavailable (state \(c.state.rawValue))")
            setReady(false)
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
        log("found \(p.name ?? "a cube")")
        c.stopScan()
        peripheral = p
        c.connect(p)
    }

    public func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        log("connected")
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
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in self?.connect() }
    }

    // MARK: discovery

    public func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard let svc = p.services?.first(where: { $0.uuid == serviceID }) else {
            log("cube service not found")
            return
        }
        p.discoverCharacteristics([payloadID, controlID, infoID], for: svc)
    }

    public func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        for ch in s.characteristics ?? [] {
            if ch.uuid == payloadID { payloadChar = ch }
            else if ch.uuid == controlID { controlChar = ch }
            else if ch.uuid == infoID { infoChar = ch }
        }
        guard let info = infoChar, controlChar != nil, payloadChar != nil else {
            log("cube is missing a characteristic")
            return
        }
        log("reading Info; if this is a new cube, macOS now asks for the code shown on its screen")
        p.readValue(for: info)
    }

    // MARK: values

    public func peripheral(_ p: CBPeripheral, didUpdateValueFor ch: CBCharacteristic, error: Error?) {
        if ch.uuid == infoID {
            if let error {
                explain(error)
                return
            }
            let b = [UInt8](ch.value ?? Data())
            guard b.first == CubeProtocol.version else {
                log("cube speaks protocol \(b.first.map(String.init) ?? "?"), this app speaks \(CubeProtocol.version); update one of them")
                return
            }
            UserDefaults.standard.set(p.identifier.uuidString, forKey: idKey)
            if let c = controlChar { p.setNotifyValue(true, for: c) }
        } else if ch.uuid == controlID, let value = ch.value, let msg = ControlMessage.parse(value) {
            switch msg {
            case .sendNow:
                onSendNow?()
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
            return
        }
        if ch.uuid == controlID, ch.isNotifying {
            log("ready")
            backoff.reset()
            setReady(true)
        }
    }

    public func peripheral(_ p: CBPeripheral, didWriteValueFor ch: CBCharacteristic, error: Error?) {
        if let error { explain(error) }
    }

    // MARK: helpers

    private func setReady(_ ready: Bool) {
        isReady = ready
        if !ready {
            awaitingAck = nil
            ackTimeout?.cancel()
        }
    }

    /// Turns the Bluetooth errors a user can act on into a sentence.
    private func explain(_ error: Error?) {
        guard let error else { return }
        if let e = error as? CBError, e.code == .peerRemovedPairingInformation {
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

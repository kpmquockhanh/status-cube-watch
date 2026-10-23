import CoreBluetooth
import Foundation
import NavCore
import os

/// The Bluetooth link to the cube: find it, pair, then stream Nav frames to it.
///
/// Flow (`docs/ble-protocol.md`): reconnect by the remembered identifier, else scan for the service
/// -> connect -> discover Info + Nav -> READ Info (encrypted, so this is what makes iOS ask for the
/// passkey the first time) -> check `[proto 1, fw_rev >= 8]` -> connected. Frames go to Nav written
/// without response, with flow control and "latest wins" (`NavChunkQueue`). Control is never
/// subscribed: its messages (acks, volume, unlock) are the Mac's business.
@MainActor
final class CubeLink: NSObject, ObservableObject {
    enum Status: Equatable { case off, searching, pairing, connected, tooOld }

    @Published private(set) var status: Status = .off
    /// Largest encoded frame that fits in 32 chunks at the negotiated write length. The ride
    /// controller copies it into `RideEngine.frameBudget` (set whenever the link is connected).
    @Published private(set) var frameBudget: Int = NavLimits.maxFrameBytes

    private static let restoreID = "cube-nav"
    private static let idKey = "cubeNavPeripheral"
    private let serviceID = CBUUID(string: CubeBLE.service)
    private let infoID = CBUUID(string: CubeBLE.info)
    private let navID = CBUUID(string: CubeBLE.nav)
    private let log = Logger(subsystem: "com.claude-cube.nav", category: "ble")

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var infoChar: CBCharacteristic?
    private var navChar: CBCharacteristic?
    private var queue = NavChunkQueue()
    private var lastScene: NavScene?
    /// `connect()` was called (or iOS restored a link): keep the cube linked from now on.
    private var wanted = false
    /// The current link is being dropped on purpose (setup failed, firmware too old): reconnect
    /// after a back-off instead of at once.
    private var droppedOnPurpose = false
    private var retryDelay: TimeInterval = 1
    private var retryPending = false

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main,
                                   options: [CBCentralManagerOptionRestoreIdentifierKey: Self.restoreID])
    }

    // MARK: public

    /// Starts (and from then on keeps) the link to the cube.
    func connect() {
        wanted = true
        start()
    }

    /// Sends one frame. Dropped while not connected; replaces a frame still waiting to go out.
    func send(_ scene: NavScene) {
        lastScene = scene
        guard status == .connected else { return }
        enqueue(scene)
    }

    // MARK: connecting

    private func start() {
        guard wanted, central.state == .poweredOn else { return }
        if let p = peripheral, p.state == .connected || p.state == .connecting {
            if p.state == .connected, navChar == nil { setUp(p) }
            return
        }
        if status != .tooOld { status = .searching }
        if let s = UserDefaults.standard.string(forKey: Self.idKey), let id = UUID(uuidString: s),
           let known = central.retrievePeripherals(withIdentifiers: [id]).first {
            log.info("reconnecting to the remembered cube")
            use(known)
            central.connect(known)  // pending until the cube is in range; never times out
        } else if let up = central.retrieveConnectedPeripherals(withServices: [serviceID]).first {
            log.info("cube already connected to this phone")
            use(up)
            central.connect(up)
        } else {
            log.info("scanning for a cube")
            central.scanForPeripherals(withServices: [serviceID])
        }
    }

    private func use(_ p: CBPeripheral) {
        peripheral = p
        p.delegate = self
    }

    private func setUp(_ p: CBPeripheral) {
        if status != .tooOld { status = .pairing }
        queue.reset()  // the cube clears its frame state on every connection
        infoChar = nil
        navChar = nil
        p.discoverServices([serviceID])
    }

    /// A setup step failed with the link still up. Drop it; the disconnect retries after a back-off.
    private func drop(_ p: CBPeripheral) {
        droppedOnPurpose = true
        navChar = nil
        central.cancelPeripheralConnection(p)
    }

    private func retryLater() {
        guard !retryPending else { return }
        retryPending = true
        let delay = status == .tooOld ? 60 : retryDelay
        retryDelay = min(retryDelay * 2, 30)
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
            guard let self else { return }
            self.retryPending = false
            self.start()
        }
    }

    private func linkLost() {
        infoChar = nil
        navChar = nil
        queue.reset()
        if status != .tooOld { status = central.state == .poweredOn ? .searching : .off }
    }

    // MARK: sending

    private func enqueue(_ scene: NavScene) {
        guard let p = peripheral, navChar != nil else { return }
        let maxWrite = p.maximumWriteValueLength(for: .withoutResponse)
        updateBudget(maxWrite)
        if queue.offer(encode(scene), maxWrite: maxWrite) == .tooLarge {
            log.error("frame too large for \(maxWrite) B writes, dropped")
        }
        pump()
    }

    /// Writes chunks while CoreBluetooth has room; `peripheralIsReady` resumes it.
    private func pump() {
        guard let p = peripheral, let nav = navChar, p.state == .connected else { return }
        while p.canSendWriteWithoutResponse, let chunk = queue.next() {
            p.writeValue(chunk, for: nav, type: .withoutResponse)
        }
    }

    private func updateBudget(_ maxWrite: Int) {
        let budget = chunkCapacity(maxWrite: maxWrite)
        if budget != frameBudget { frameBudget = budget }
    }

    private func becameReady(_ p: CBPeripheral) {
        let stored = UserDefaults.standard.string(forKey: Self.idKey)
        let firstTime = stored != p.identifier.uuidString
        UserDefaults.standard.set(p.identifier.uuidString, forKey: Self.idKey)
        retryDelay = 1
        updateBudget(p.maximumWriteValueLength(for: .withoutResponse))
        status = .connected
        log.info("connected (first pairing: \(firstTime)), frame budget \(self.frameBudget) B")
        // A new bond's kind is learned from its first write, and a new bond whose link drops before
        // that write is deleted: identify as the iPhone at once. On a reconnect, resend what the
        // cube should be showing (the ride keeps sending its own frames anyway).
        enqueue(firstTime ? .ended : (lastScene ?? .ended))
    }

    /// Finds Info and Nav among already-known characteristics (a restored link), else discovers.
    private func findCharacteristics(_ p: CBPeripheral, in svc: CBService) {
        for ch in svc.characteristics ?? [] {
            if ch.uuid == infoID { infoChar = ch } else if ch.uuid == navID { navChar = ch }
        }
        guard let info = infoChar else {
            log.error("cube has no Info characteristic")
            drop(p)
            return
        }
        // Read Info even when Nav is missing: an old firmware is reported as tooOld, not a failure.
        log.info("reading Info (a new cube asks for the code on its screen now)")
        p.readValue(for: info)
    }
}

// MARK: - CBCentralManagerDelegate

extension CubeLink: @preconcurrency CBCentralManagerDelegate {
    func centralManager(_ c: CBCentralManager, willRestoreState dict: [String: Any]) {
        let restored = dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral] ?? []
        let stored = UserDefaults.standard.string(forKey: Self.idKey)
        guard let p = restored.first(where: { $0.identifier.uuidString == stored }) ?? restored.first else { return }
        log.info("restored by iOS, link state \(p.state.rawValue)")
        wanted = true
        use(p)
        // Setup resumes once the central reports poweredOn (start()).
    }

    func centralManagerDidUpdateState(_ c: CBCentralManager) {
        switch c.state {
        case .poweredOn:
            if wanted { start() } else { status = .off }
        default:
            log.info("Bluetooth unavailable (state \(c.state.rawValue))")
            infoChar = nil
            navChar = nil
            queue.reset()
            status = .off
        }
    }

    func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi: NSNumber) {
        log.info("found \(p.name ?? "a cube")")
        c.stopScan()
        use(p)
        c.connect(p)
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        guard p.identifier == peripheral?.identifier else { return }
        droppedOnPurpose = false
        setUp(p)
    }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        guard p.identifier == peripheral?.identifier else { return }
        log.error("connect failed: \(error?.localizedDescription ?? "?")")
        linkLost()
        retryLater()
    }

    func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        guard p.identifier == peripheral?.identifier else { return }
        log.info("disconnected: \(error?.localizedDescription ?? "no error")")
        if let e = error as? CBError, e.code == .peerRemovedPairingInformation {
            log.error("the cube forgot this iPhone: remove Claude Cube in Settings > Bluetooth, then pair again")
        }
        linkLost()
        guard wanted, c.state == .poweredOn else { return }
        if droppedOnPurpose {
            droppedOnPurpose = false
            retryLater()
        } else {
            // A pending connect is kept by iOS even while the app is suspended in the background.
            c.connect(p)
        }
    }
}

// MARK: - CBPeripheralDelegate

extension CubeLink: @preconcurrency CBPeripheralDelegate {
    func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard p.identifier == peripheral?.identifier else { return }
        guard error == nil, let svc = p.services?.first(where: { $0.uuid == serviceID }) else {
            log.error("cube service not found: \(error?.localizedDescription ?? "missing")")
            drop(p)
            return
        }
        if svc.characteristics?.contains(where: { $0.uuid == infoID }) == true {
            findCharacteristics(p, in: svc)
        } else {
            p.discoverCharacteristics([infoID, navID], for: svc)
        }
    }

    func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        guard p.identifier == peripheral?.identifier else { return }
        if let error {
            log.error("characteristic discovery failed: \(error.localizedDescription)")
            drop(p)
            return
        }
        findCharacteristics(p, in: s)
    }

    func peripheral(_ p: CBPeripheral, didModifyServices invalidatedServices: [CBService]) {
        guard p.identifier == peripheral?.identifier,
              invalidatedServices.contains(where: { $0.uuid == serviceID }) else { return }
        log.info("cube's GATT table changed; rediscovering")
        setUp(p)
    }

    func peripheral(_ p: CBPeripheral, didUpdateValueFor ch: CBCharacteristic, error: Error?) {
        guard p.identifier == peripheral?.identifier, ch.uuid == infoID else { return }  // Control is ignored
        if let error {
            log.error("Info read failed (pairing cancelled or wrong code?): \(error.localizedDescription)")
            drop(p)
            return
        }
        let info = [UInt8](ch.value ?? Data())
        guard CubeBLE.infoSupportsNav(info) else {
            log.error("cube firmware too old for navigation: Info \(info)")
            status = .tooOld
            drop(p)
            return
        }
        guard navChar != nil else {
            // fw_rev 8+ without Nav listed: iOS is serving a cached GATT table.
            log.error("cube reports Nav but iOS did not list it: remove Claude Cube in Settings > Bluetooth and pair again")
            status = .tooOld
            drop(p)
            return
        }
        becameReady(p)
    }

    func peripheralIsReady(toSendWriteWithoutResponse p: CBPeripheral) {
        guard p.identifier == peripheral?.identifier else { return }
        pump()
    }
}

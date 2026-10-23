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
/// subscribed: its messages (acks, volume, unlock) are the Mac's business. What happens after a
/// link drops is `NavLinkPolicy.onDrop` (pure, tested).
@MainActor
final class CubeLink: NSObject, ObservableObject {
    enum Status: Equatable {
        /// Bluetooth is off or unavailable, or `connect()` was not called yet.
        case off
        /// Bluetooth permission denied: allow CubeNav in Settings > Privacy & Security > Bluetooth.
        case unauthorized
        case searching
        /// Connected, reading Info: iOS asks for the passkey on a new cube.
        case pairing
        /// Pairing has not completed 30 s after it started: the cube only accepts a second phone
        /// in its pairing window (portal: "Pair new device"). The link attempt keeps going.
        case openPairingWindow
        case connected
        /// The cube's firmware has no Nav (needs proto 1, fw_rev 8+), or iOS caches an old table.
        case tooOld
        /// The cube deleted this iPhone's bond: forget "Claude Cube" in Settings > Bluetooth, then
        /// call `connect()` again (e.g. on return to the foreground).
        case forgetCube
    }

    @Published private(set) var status: Status = .off
    /// Largest encoded frame that fits in 32 chunks at the negotiated write length. The ride
    /// controller copies it into `RideEngine.frameBudget` (set whenever the link is connected).
    @Published private(set) var frameBudget: Int = NavLimits.maxFrameBytes

    private static let restoreID = "cube-nav"
    private static let idKey = "cubeNavPeripheral"
    private static let pairingHintDelay: TimeInterval = 30
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
    private var lastSceneAt = Date.distantPast
    /// `connect()` was called (or iOS restored a link): keep the cube linked from now on.
    private var wanted = false
    /// The current link reached `connected`.
    private var wasReady = false
    /// Cubes found by scanning that never became ready: not tried again for 60 s.
    private var skipped = NavSkipList()
    /// Cubes that deleted this iPhone's bond: not tried again until `connect()`.
    private var blocked: Set<UUID> = []
    private var retryDelay: TimeInterval = 1
    private var retryPending = false
    private var pairingHint: DispatchWorkItem?

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main,
                                   options: [CBCentralManagerOptionRestoreIdentifierKey: Self.restoreID])
    }

    // MARK: public

    /// Starts (and from then on keeps) the link to the cube. Calling it again (e.g. on return to the
    /// foreground) also retries cubes skipped or given up on earlier in the session.
    func connect() {
        wanted = true
        skipped.removeAll()
        if !blocked.isEmpty {
            blocked.removeAll()
            if status == .forgetCube { status = .searching }
        }
        start()
    }

    /// Sends one frame. Dropped while not connected; replaces a frame still waiting to go out.
    func send(_ scene: NavScene) {
        lastScene = scene
        lastSceneAt = Date()
        guard status == .connected else { return }
        enqueue(scene)
    }

    /// For a replaced cube: forgets the remembered identifier and the skip lists, drops the link and
    /// searches again. The old cube is passed over for a minute while its link winds down, so the
    /// search does not pick it straight back up.
    func forget() {
        log.info("forgetting the remembered cube")
        UserDefaults.standard.removeObject(forKey: Self.idKey)
        skipped.removeAll()
        blocked.removeAll()
        cancelPairingHint()
        retryDelay = 1
        wasReady = false
        if let old = peripheral {
            peripheral = nil   // its disconnect callback is ignored from here on
            if old.state == .connected || old.state == .connecting { central.cancelPeripheralConnection(old) }
            skipped.skip(old.identifier, at: Date())
            DispatchQueue.main.asyncAfter(deadline: .now() + NavSkipList.ttl + 1) { [weak self] in
                self?.rescanIfIdle()
            }
        }
        infoChar = nil
        navChar = nil
        queue.reset()
        wanted = true
        guard central.state == .poweredOn else { return }
        status = .searching
        central.stopScan()
        start()
    }

    // MARK: status

    /// Sets a passing status, unless one that needs the user's attention is showing.
    private func setTransient(_ s: Status) {
        switch status {
        case .tooOld, .openPairingWindow, .forgetCube: return
        default: status = s
        }
    }

    // MARK: connecting

    private func start() {
        guard wanted, central.state == .poweredOn else { return }
        if let p = peripheral, p.state == .connected || p.state == .connecting {
            if p.state == .connected, navChar == nil { setUp(p, resetQueue: true) }
            return
        }
        setTransient(.searching)
        let usable = { (p: CBPeripheral) in !self.skipped.contains(p.identifier, now: Date()) && !self.blocked.contains(p.identifier) }
        if let s = UserDefaults.standard.string(forKey: Self.idKey), let id = UUID(uuidString: s),
           let known = central.retrievePeripherals(withIdentifiers: [id]).first, usable(known) {
            log.info("reconnecting to the remembered cube")
            use(known)
            central.connect(known)  // pending until the cube is in range; never times out
        } else if let up = central.retrieveConnectedPeripherals(withServices: [serviceID]).first(where: usable) {
            log.info("cube already connected to this phone")
            use(up)
            central.connect(up)
        } else {
            log.info("scanning for a cube")
            peripheral = nil
            central.scanForPeripherals(withServices: [serviceID])
        }
    }

    private func use(_ p: CBPeripheral) {
        peripheral = p
        p.delegate = self
    }

    private func setUp(_ p: CBPeripheral, resetQueue: Bool) {
        setTransient(.pairing)
        if resetQueue { queue.reset() }  // the cube clears its frame state on every connection
        infoChar = nil
        navChar = nil
        p.discoverServices([serviceID])
    }

    /// A setup step failed with the link still up. Drop it; `onDrop` decides what comes next.
    private func drop(_ p: CBPeripheral) {
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
        setTransient(central.state == .poweredOn ? .searching : .off)
    }

    /// Shows `openPairingWindow` if no link becomes ready within 30 s of the first Info read of a
    /// cube that is not the remembered one (a bonded cube reconnecting slowly needs no new pairing).
    /// Kept across retries; cancelled once a link is ready or Bluetooth goes away.
    private func armPairingHint(for p: CBPeripheral) {
        guard pairingHint == nil,
              UserDefaults.standard.string(forKey: Self.idKey) != p.identifier.uuidString else { return }
        let item = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.pairingHint = nil
            if self.status != .connected, self.status != .tooOld, self.status != .forgetCube {
                self.log.info("pairing not done after 30 s: open Pair new device in the cube's portal")
                self.status = .openPairingWindow
                // Give skipped cubes (perhaps this user's own, after a cancelled dialog) another try.
                self.skipped.removeAll()
                self.rescanIfIdle()
            }
        }
        pairingHint = item
        DispatchQueue.main.asyncAfter(deadline: .now() + Self.pairingHintDelay, execute: item)
    }

    /// Restarts the search when no link is up or being made (restarting a scan re-reports cubes).
    private func rescanIfIdle() {
        guard wanted, central.state == .poweredOn,
              peripheral?.state != .connected, peripheral?.state != .connecting else { return }
        central.stopScan()
        start()
    }

    private func cancelPairingHint() {
        pairingHint?.cancel()
        pairingHint = nil
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
        wasReady = true
        cancelPairingHint()
        updateBudget(p.maximumWriteValueLength(for: .withoutResponse))
        status = .connected
        log.info("connected (first pairing: \(firstTime)), frame budget \(self.frameBudget) B")
        // A new bond's kind is learned from its first write, and a new bond whose link drops before
        // that write is deleted: identify as the iPhone at once. On a reconnect, resend a fresh
        // last scene; a stale one could start a ghost ride, so that becomes Ended.
        enqueue(NavLinkPolicy.firstFrame(firstPairing: firstTime, last: lastScene,
                                         age: Date().timeIntervalSince(lastSceneAt)))
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
        armPairingHint(for: p)
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
            if wanted {
                if status == .off || status == .unauthorized { status = .searching }
                start()
            } else {
                status = .off
            }
        default:
            log.info("Bluetooth unavailable (state \(c.state.rawValue))")
            infoChar = nil
            navChar = nil
            queue.reset()
            cancelPairingHint()
            status = c.state == .unauthorized ? .unauthorized : .off
        }
    }

    func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral,
                        advertisementData: [String: Any], rssi: NSNumber) {
        guard !skipped.contains(p.identifier, now: Date()), !blocked.contains(p.identifier) else { return }
        // Never orphan a connection already in flight.
        guard peripheral == nil || peripheral?.state == .disconnected else { return }
        log.info("found \(p.name ?? "a cube")")
        c.stopScan()
        use(p)
        c.connect(p)
    }

    func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        guard p.identifier == peripheral?.identifier else { return }
        wasReady = false
        setUp(p, resetQueue: true)
    }

    func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        guard p.identifier == peripheral?.identifier else { return }
        log.error("connect failed: \(error?.localizedDescription ?? "?")")
        handleDrop(p, error: error)
    }

    func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        guard p.identifier == peripheral?.identifier else { return }
        log.info("disconnected: \(error?.localizedDescription ?? "no error")")
        handleDrop(p, error: error)
    }

    private func handleDrop(_ p: CBPeripheral, error: Error?) {
        let peerRemoved = (error as? CBError)?.code == .peerRemovedPairingInformation
        let remembered = UserDefaults.standard.string(forKey: Self.idKey) == p.identifier.uuidString
        let action = NavLinkPolicy.onDrop(wasReady: wasReady, remembered: remembered,
                                          tooOld: status == .tooOld, peerRemovedPairing: peerRemoved)
        wasReady = false
        linkLost()
        guard wanted, central.state == .poweredOn else { return }
        switch action {
        case .reconnectNow:
            // A pending connect is kept by iOS even while the app is suspended in the background.
            central.connect(p)
        case .retryLater:
            retryLater()
        case .skipAndScan:
            log.info("skipping cube \(p.identifier) for this session; scanning again")
            skipped.skip(p.identifier, at: Date())
            peripheral = nil
            start()
            // A running scan does not report a cube it has already seen: restart it once the skip
            // expires, so that cube can be found again.
            DispatchQueue.main.asyncAfter(deadline: .now() + NavSkipList.ttl + 1) { [weak self] in
                self?.rescanIfIdle()
            }
        case .needsForget:
            log.error("the cube forgot this iPhone: forget Claude Cube in Settings > Bluetooth, then pair again")
            blocked.insert(p.identifier)
            peripheral = nil
            cancelPairingHint()
            status = .forgetCube
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
        // Same connection: the cube keeps its frame state, so the queue and seq carry on.
        setUp(p, resetQueue: false)
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
            cancelPairingHint()
            status = .tooOld
            drop(p)
            return
        }
        guard navChar != nil else {
            // fw_rev 8+ without Nav listed: iOS is serving a cached GATT table.
            log.error("cube reports Nav but iOS did not list it: remove Claude Cube in Settings > Bluetooth and pair again")
            cancelPairingHint()
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

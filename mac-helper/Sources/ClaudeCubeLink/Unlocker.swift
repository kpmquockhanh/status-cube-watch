import AppKit
import ApplicationServices
import IOKit.pwr_mgt
import OpenDirectory
import Security

/// Unlock with cube: the login password in the Keychain, typed into the lock screen when the cube asks
/// (Control `07`; `UnlockPolicy` decides whether to). The password never goes to the cube.
///
/// Typing needs the Accessibility permission. It works on a locked session only: after a restart the
/// FileVault login comes before this app runs.
final class Unlocker {
    private static let service = "com.claude-cube.link"
    private static let account = "unlock"

    private let log: (String) -> Void
    /// Read once at launch: the app is ad-hoc signed, so the Keychain may ask for access, and nobody
    /// can answer that dialog while the screen is locked.
    private var password: String?

    init(log: @escaping (String) -> Void) {
        self.log = log
        password = load()
    }

    var isEnabled: Bool { password != nil }
    var isTrusted: Bool { AXIsProcessTrusted() }

    /// The menu's "Unlock Mac with cube": asks for the login password, checks it, stores it.
    func enableInteractively() {
        if !isTrusted {
            AXIsProcessTrustedWithOptions([kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary)
        }
        var note = "The cube asks; this Mac types the password into the lock screen. It stays in your Keychain."
        while let pw = Self.askPassword(note: note) {
            guard Self.verify(pw) else {
                note = "That is not the login password for \(NSUserName()). Try again."
                continue
            }
            let status = save(pw)
            guard status == errSecSuccess else {
                let why = SecCopyErrorMessageString(status, nil) as String? ?? "error \(status)"
                log("could not save the unlock password to the Keychain: \(why)")
                return
            }
            password = pw
            log("Unlock with cube on" + (isTrusted ? "" : " (waiting for the Accessibility permission)"))
            return
        }
    }

    func disable() {
        let q = query
        let status = SecItemDelete(q as CFDictionary)
        if status != errSecSuccess && status != errSecItemNotFound {
            log("could not remove the unlock password from the Keychain: error \(status)")
        }
        password = nil
        log("Unlock with cube off")
    }

    /// Wakes the display and types the password + Return. Call only when `UnlockPolicy` says `.type`.
    func unlock() {
        guard let pw = password else { return }
        let asleep = CGDisplayIsAsleep(CGMainDisplayID()) != 0
        var id: IOPMAssertionID = 0
        IOPMAssertionDeclareUserActivity("Claude Cube unlock" as CFString, kIOPMUserActiveLocal, &id)
        // A display that was off drops the first keys while the lock screen comes up.
        DispatchQueue.global(qos: .userInitiated).asyncAfter(deadline: .now() + (asleep ? 1.2 : 0.3)) { [log] in
            // A login password changed since it was stored must not be typed: wrong guesses count.
            guard Self.verify(pw) else {
                DispatchQueue.main.async {
                    log("the stored unlock password no longer works: turn Unlock Mac with cube off and on again")
                }
                return
            }
            Self.type(pw)
        }
    }

    private static func type(_ s: String) {
        let src = CGEventSource(stateID: .hidSystemState)
        for c in s {
            var units = Array(String(c).utf16)
            for down in [true, false] {
                guard let e = CGEvent(keyboardEventSource: src, virtualKey: 0, keyDown: down) else { continue }
                e.keyboardSetUnicodeString(stringLength: units.count, unicodeString: &units)
                e.post(tap: .cghidEventTap)
            }
            usleep(8_000)
        }
        for down in [true, false] {
            CGEvent(keyboardEventSource: src, virtualKey: 0x24, keyDown: down)?.post(tap: .cghidEventTap)  // Return
        }
    }

    /// Whether `pw` is this user's login password, so a typo is never typed into the lock screen.
    private static func verify(_ pw: String) -> Bool {
        do {
            let node = try ODNode(session: ODSession.default(), type: ODNodeType(kODNodeTypeAuthentication))
            let record = try node.record(withRecordType: kODRecordTypeUsers, name: NSUserName(), attributes: nil)
            try record.verifyPassword(pw)
            return true
        } catch {
            return false
        }
    }

    private static func askPassword(note: String) -> String? {
        NSApp.activate(ignoringOtherApps: true)  // an accessory app's alert would open behind the front app
        let alert = NSAlert()
        alert.messageText = "Unlock this Mac with a tap on the cube"
        alert.informativeText = note
        let field = NSSecureTextField(frame: NSRect(x: 0, y: 0, width: 240, height: 24))
        field.placeholderString = "Login password for \(NSUserName())"
        alert.accessoryView = field
        alert.addButton(withTitle: "Turn On")
        alert.addButton(withTitle: "Cancel")
        alert.window.initialFirstResponder = field
        guard alert.runModal() == .alertFirstButtonReturn, !field.stringValue.isEmpty else { return nil }
        return field.stringValue
    }

    private var query: [String: Any] {
        [kSecClass as String: kSecClassGenericPassword,
         kSecAttrService as String: Self.service,
         kSecAttrAccount as String: Self.account]
    }

    private func load() -> String? {
        var q = query
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var out: CFTypeRef?
        let status = SecItemCopyMatching(q as CFDictionary, &out)
        if status == errSecItemNotFound { return nil }
        guard status == errSecSuccess, let data = out as? Data else {
            log("could not read the unlock password from the Keychain: error \(status)")
            return nil
        }
        return String(data: data, encoding: .utf8)
    }

    private func save(_ pw: String) -> OSStatus {
        let data = Data(pw.utf8)
        let status = SecItemUpdate(query as CFDictionary, [kSecValueData as String: data] as CFDictionary)
        guard status == errSecItemNotFound else { return status }
        var add = query
        add[kSecValueData as String] = data
        add[kSecAttrLabel as String] = "Claude Cube Link unlock"
        return SecItemAdd(add as CFDictionary, nil)
    }
}

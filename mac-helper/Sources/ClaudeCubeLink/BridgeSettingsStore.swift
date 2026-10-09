import CubeLinkCore
import Foundation
import Security

/// Where the bridge settings live: the plain ones in the app's defaults (`bridgeSettings`, JSON),
/// the secrets in one generic-password Keychain item. Both are read once and cached, so a bridge
/// restart does not touch the Keychain again.
final class BridgeSettingsStore {
    private static let defaultsKey = "bridgeSettings"
    private static let service = "com.claude-cube.link"
    private static let account = "bridge"

    private let defaults: UserDefaults
    private let log: (String) -> Void
    private(set) var settings: BridgeSettings
    private(set) var secrets: BridgeSecrets

    init(defaults: UserDefaults = .standard, log: @escaping (String) -> Void) {
        self.defaults = defaults
        self.log = log
        settings = defaults.data(forKey: Self.defaultsKey)
            .flatMap { try? JSONDecoder().decode(BridgeSettings.self, from: $0) } ?? BridgeSettings()
        secrets = BridgeSecrets()
        secrets = loadSecrets()
    }

    /// Nothing saved yet: a fresh install, or one from before these settings existed.
    var isEmpty: Bool { defaults.data(forKey: Self.defaultsKey) == nil }

    /// Saves both halves. Returns why the Keychain refused, or nil.
    @discardableResult
    func save(_ s: BridgeSettings, _ secret: BridgeSecrets) -> String? {
        if let data = try? JSONEncoder().encode(s) { defaults.set(data, forKey: Self.defaultsKey) }
        settings = s
        guard secret != secrets else { return nil }
        let status = writeSecrets(secret)
        guard status == errSecSuccess else {
            let why = SecCopyErrorMessageString(status, nil) as String? ?? "error \(status)"
            log("could not save bridge secrets to the Keychain: \(why)")
            return why
        }
        secrets = secret
        return nil
    }

    /// One-time import of a bridge/config.json, which install.sh names in the defaults (`importConfig`).
    /// Settings already saved in the app win, so the import only fills an empty store. The key is
    /// removed either way; the file itself is left where it is.
    func importConfigIfOffered() {
        guard let path = defaults.string(forKey: "importConfig") else { return }
        defaults.removeObject(forKey: "importConfig")
        guard isEmpty else { return }
        guard let data = FileManager.default.contents(atPath: path),
              let (s, secret) = BridgeSettings.imported(configJSON: data) else {
            log("could not import bridge settings from \(path)")
            return
        }
        if save(s, secret) == nil { log("imported bridge settings from \(path)") }
    }

    private var query: [String: Any] {
        [kSecClass as String: kSecClassGenericPassword,
         kSecAttrService as String: Self.service,
         kSecAttrAccount as String: Self.account]
    }

    private func loadSecrets() -> BridgeSecrets {
        var q = query
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var out: CFTypeRef?
        let status = SecItemCopyMatching(q as CFDictionary, &out)
        if status == errSecItemNotFound { return BridgeSecrets() }
        guard status == errSecSuccess, let data = out as? Data,
              let s = try? JSONDecoder().decode(BridgeSecrets.self, from: data) else {
            let why = SecCopyErrorMessageString(status, nil) as String? ?? "error \(status)"
            log("could not read bridge secrets from the Keychain: \(why)")
            return BridgeSecrets()
        }
        return s
    }

    private func writeSecrets(_ s: BridgeSecrets) -> OSStatus {
        if s.isEmpty {
            let status = SecItemDelete(query as CFDictionary)
            return status == errSecItemNotFound ? errSecSuccess : status
        }
        guard let data = try? JSONEncoder().encode(s) else { return errSecParam }
        let status = SecItemUpdate(query as CFDictionary, [kSecValueData as String: data] as CFDictionary)
        guard status == errSecItemNotFound else { return status }
        var add = query
        add[kSecValueData as String] = data
        add[kSecAttrLabel as String] = "Claude Cube Link bridge settings"
        return SecItemAdd(add as CFDictionary, nil)
    }
}

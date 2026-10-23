import Foundation
import NavCore
import Security

/// Where the nav server is and the key to use it. The URL is a plain preference; the key is a
/// Keychain item and never leaves the phone except in the `X-Nav-Key` header.
enum ServerConfig {
    static let defaultURL = "https://nav.devxdev.site"
    private static let urlKey = "serverURL"
    private static let service = "com.claude-cube.nav", account = "nav-key"

    static var urlString: String {
        get { UserDefaults.standard.string(forKey: urlKey) ?? defaultURL }
        set { UserDefaults.standard.set(newValue.trimmingCharacters(in: .whitespacesAndNewlines), forKey: urlKey) }
    }

    static var key: String {
        get {
            let q: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service,
                                    kSecAttrAccount as String: account, kSecReturnData as String: true,
                                    kSecMatchLimit as String: kSecMatchLimitOne]
            var out: CFTypeRef?
            guard SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess, let d = out as? Data else { return "" }
            return String(decoding: d, as: UTF8.self)
        }
        set {
            let base: [String: Any] = [kSecClass as String: kSecClassGenericPassword, kSecAttrService as String: service,
                                       kSecAttrAccount as String: account]
            SecItemDelete(base as CFDictionary)
            let k = newValue.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !k.isEmpty else { return }
            var add = base
            add[kSecValueData as String] = Data(k.utf8)
            add[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlock   // readable while the phone is locked
            SecItemAdd(add as CFDictionary, nil)
        }
    }

    /// nil when the URL is not usable or there is no key yet.
    static func makeServer() -> HTTPNavServer? {
        guard let url = URL(string: urlString), url.scheme?.hasPrefix("http") == true, !key.isEmpty else { return nil }
        return HTTPNavServer(baseURL: url, key: key)
    }

    static func expiry() -> Date? {
        guard let u = Bundle.main.url(forResource: "embedded", withExtension: "mobileprovision"),
              let d = try? Data(contentsOf: u) else { return nil }
        return provisionExpiry(d)
    }
}

import Foundation

/// The bridge's settings as the app keeps them ("Bridge settings…"), in place of bridge/config.json.
/// They reach the bridge child as the environment variables server.mjs already reads, which win
/// over any config.json. The secrets are kept apart, in the Keychain (`BridgeSecrets`).
public struct BridgeSettings: Codable, Equatable {
    public enum Source: String, Codable, CaseIterable { case local, admin }

    public var source: Source = .local
    /// The spend/token cards after the rate-limit rings (CUBE_EXTRA_CARDS).
    public var extraCards = false
    /// Seconds between refreshes; nil = the bridge's default (5 s local, 60 s admin).
    public var refreshSec: Int?
    /// Listen on every interface, which a cube polling over WiFi needs; off = 127.0.0.1 only.
    public var lan = true
    public var gmailUser = ""

    public static let refreshRange = 1...86_400

    /// Every variable `environment` decides. The inherited ones are dropped first, so a value the
    /// settings leave unset (say `lan` on, no CUBE_HOST) is not filled in from the app's own environment.
    public static let managedKeys: Set<String> = [
        "CUBE_SOURCE", "CUBE_EXTRA_CARDS", "CUBE_REFRESH_MS", "CUBE_HOST",
        "ANTHROPIC_ADMIN_KEY", "ANTHROPIC_ADMIN_OAUTH_TOKEN", "CUBE_GMAIL_USER", "CUBE_GMAIL_PASSWORD",
    ]

    public init() {}

    // Missing keys keep their defaults, so settings saved by an older build still load.
    private enum CodingKeys: String, CodingKey { case source, extraCards, refreshSec, lan, gmailUser }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        source = (try? c.decodeIfPresent(Source.self, forKey: .source)) ?? .local
        extraCards = (try? c.decodeIfPresent(Bool.self, forKey: .extraCards)) ?? false
        refreshSec = try? c.decodeIfPresent(Int.self, forKey: .refreshSec)
        lan = (try? c.decodeIfPresent(Bool.self, forKey: .lan)) ?? true
        gmailUser = (try? c.decodeIfPresent(String.self, forKey: .gmailUser)) ?? ""
    }

    /// The bridge child's environment: `inherited` minus `managedKeys`, plus these settings.
    public func environment(inherited: [String: String], secrets: BridgeSecrets) -> [String: String] {
        var env = inherited.filter { !Self.managedKeys.contains($0.key) }
        env["CUBE_SOURCE"] = source.rawValue
        env["CUBE_EXTRA_CARDS"] = extraCards ? "1" : "0"
        if let r = refreshSec { env["CUBE_REFRESH_MS"] = String(r * 1000) }
        if !lan { env["CUBE_HOST"] = "127.0.0.1" }
        if !secrets.adminKey.isEmpty { env["ANTHROPIC_ADMIN_KEY"] = secrets.adminKey }
        if !secrets.oauthToken.isEmpty { env["ANTHROPIC_ADMIN_OAUTH_TOKEN"] = secrets.oauthToken }
        if !gmailUser.isEmpty, !secrets.gmailPassword.isEmpty {
            env["CUBE_GMAIL_USER"] = gmailUser
            env["CUBE_GMAIL_PASSWORD"] = secrets.gmailPassword
        }
        return env
    }

    /// Why these settings cannot be saved, or nil when they can.
    public func problem(secrets: BridgeSecrets) -> String? {
        if let r = refreshSec, !Self.refreshRange.contains(r) {
            return "Refresh must be \(Self.refreshRange.lowerBound)–\(Self.refreshRange.upperBound) seconds."
        }
        // server.mjs builds the admin source at startup and exits without a credential.
        if source == .admin, secrets.adminKey.isEmpty, secrets.oauthToken.isEmpty {
            return "The Admin API source needs an admin key or an OAuth token."
        }
        if gmailUser.isEmpty != secrets.gmailPassword.isEmpty {
            return "Mail needs both the Gmail address and an app password, or neither."
        }
        return nil
    }

    /// Reads a bridge/config.json (the one-time import install.sh arranges). Unknown and "//" comment
    /// keys are ignored, as is a value server.mjs would ignore; `port` is the app's own (CUBE_PORT).
    public static func imported(configJSON: Data) -> (BridgeSettings, BridgeSecrets)? {
        guard let o = try? JSONSerialization.jsonObject(with: configJSON) as? [String: Any] else { return nil }
        var s = BridgeSettings()
        var secrets = BridgeSecrets()
        if let v = o["source"] as? String, let src = Source(rawValue: v) { s.source = src }
        if let v = o["extraCards"] { s.extraCards = truthy(v) }
        if let ms = (o["refreshMs"] as? NSNumber)?.doubleValue ?? Double(o["refreshMs"] as? String ?? ""),
           ms >= 1000, ms <= Double(refreshRange.upperBound) * 1000 {
            s.refreshSec = Int((ms / 1000).rounded())
        }
        if let h = o["host"] as? String, !h.isEmpty {
            s.lan = !(h.hasPrefix("127.") || h == "::1" || h == "localhost")
        }
        secrets.adminKey = o["adminKey"] as? String ?? ""
        secrets.oauthToken = o["oauthToken"] as? String ?? ""
        if let g = o["gmail"] as? [String: Any] {
            s.gmailUser = g["user"] as? String ?? ""
            secrets.gmailPassword = g["appPassword"] as? String ?? ""
        }
        return (s, secrets)
    }

    /// server.mjs's `bool`: JSON true/false, or 1/true/yes/on as text.
    private static func truthy(_ v: Any) -> Bool {
        if let b = v as? Bool { return b }
        if let n = v as? NSNumber { return n.intValue != 0 }
        if let s = v as? String { return ["1", "true", "yes", "on"].contains(s.trimmingCharacters(in: .whitespaces).lowercased()) }
        return false
    }
}

/// The bridge settings that are secrets: stored together as one Keychain item, so a rebuilt
/// (re-signed) app asks for Keychain access once, not once per secret.
public struct BridgeSecrets: Codable, Equatable {
    public var adminKey = ""
    public var oauthToken = ""
    public var gmailPassword = ""

    public init(adminKey: String = "", oauthToken: String = "", gmailPassword: String = "") {
        self.adminKey = adminKey
        self.oauthToken = oauthToken
        self.gmailPassword = gmailPassword
    }

    public var isEmpty: Bool { adminKey.isEmpty && oauthToken.isEmpty && gmailPassword.isEmpty }
}

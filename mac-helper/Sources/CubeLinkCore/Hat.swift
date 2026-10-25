import Foundation

extension BridgeClient {
    /// `POST /api/hat {"agent", "hat": 0...103 | -1 (none) | null (auto)}`, with the loopback guard's header.
    public static func hatRequest(port: Int, agent: String, hat: HatChoice) -> URLRequest {
        var r = URLRequest(url: URL(string: "http://127.0.0.1:\(port)/api/hat")!)
        r.httpMethod = "POST"
        r.timeoutInterval = 5
        r.setValue("application/json", forHTTPHeaderField: "Content-Type")
        r.setValue("1", forHTTPHeaderField: "X-Cube-Client")
        let value: Any
        switch hat {
        case .code(let c): value = c
        case .none: value = -1
        case .auto: value = NSNull()
        }
        r.httpBody = try? JSONSerialization.data(withJSONObject: ["agent": agent, "hat": value], options: [.sortedKeys])
        return r
    }

    /// `done(status, nil)` for any HTTP response, `done(nil, reason)` on a transport error.
    /// Sent once, never retried (fresh ephemeral session). May be called on any queue.
    public func setHat(agent: String, hat: HatChoice, _ done: @escaping (Int?, String?) -> Void) {
        let cfg = URLSessionConfiguration.ephemeral
        cfg.timeoutIntervalForRequest = 5
        cfg.waitsForConnectivity = false
        let once = URLSession(configuration: cfg)
        defer { once.finishTasksAndInvalidate() }
        once.dataTask(with: BridgeClient.hatRequest(port: port, agent: agent, hat: hat)) { _, resp, err in
            if let http = resp as? HTTPURLResponse { return done(http.statusCode, nil) }
            done(nil, err?.localizedDescription ?? "no HTTP response")
        }.resume()
    }
}

/// 204 set, 404 unknown agent or tracking off; any other status is a refusal, none is a transport failure.
public func hatOutcome(status: Int?, error: String?) -> String {
    guard let status else { return "no reply from the bridge: \(error ?? "no response")" }
    switch status {
    case 204: return "set"
    case 404: return "agent unknown or tracking is off (404)"
    default: return "refused by the bridge (HTTP \(status))"
    }
}

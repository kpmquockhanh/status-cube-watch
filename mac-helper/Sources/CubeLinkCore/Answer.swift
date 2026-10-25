import Foundation

/// When a tap on a cube prompt (Control `09`) may become a POST to the bridge.
///
/// Never while the Mac is locked (someone at the desk must not approve a command through the cube),
/// and one at a time: a second `09` while a POST is out is dropped, not queued.
public struct AnswerPolicy {
    public enum Decision: Equatable {
        case post
        case ignore(String)
    }

    private var inFlight = false

    public init() {}

    public mutating func decide(locked: Bool) -> Decision {
        if locked { return .ignore("the Mac is locked") }
        if inFlight { return .ignore("an answer is still being sent") }
        inFlight = true
        return .post
    }

    /// The POST completed, whatever the outcome.
    public mutating func finished() { inFlight = false }
}

/// Whether the login session is locked or not on the console, read from `CGSessionCopyCurrentDictionary()`.
/// True when there is no dictionary, when `CGSSessionScreenIsLocked` is set, or when
/// `kCGSSessionOnConsoleKey` is false or missing (fast user switching: another user has the screen).
/// Values may come as Bool or NSNumber.
public func sessionLocked(_ session: [String: Any]?) -> Bool {
    guard let session else { return true }
    func flag(_ key: String) -> Bool? {
        if let b = session[key] as? Bool { return b }
        return (session[key] as? NSNumber)?.boolValue
    }
    if flag("CGSSessionScreenIsLocked") == true { return true }
    return flag("kCGSSessionOnConsoleKey") != true
}

public enum AnswerOutcome: Equatable {
    case answered
    case notNow
    case trackingOff
    case rejected(Int)
    case failed(String)
}

/// 204 answered, 409 not answerable now, 404 tracking off; any other status is a refusal,
/// no status is a transport failure.
public func answerOutcome(status: Int?, error: String?) -> AnswerOutcome {
    guard let status else { return .failed(error ?? "no response") }
    switch status {
    case 204: return .answered
    case 409: return .notNow
    case 404: return .trackingOff
    default: return .rejected(status)
    }
}

public func describe(_ o: AnswerOutcome) -> String {
    switch o {
    case .answered: return "answered"
    case .notNow: return "not answerable now (409)"
    case .trackingOff: return "agent tracking is off or the agent is gone (404)"
    case .rejected(let code): return "refused by the bridge (HTTP \(code))"
    case .failed(let reason): return "no reply from the bridge: \(reason)"
    }
}

extension BridgeClient {
    /// The loopback request the bridge's guard accepts: `Host` is `127.0.0.1:<port>`, no `Origin`,
    /// and the `X-Cube-Client` header. The ids go through exactly as the cube sent them.
    public static func answerRequest(port: Int, agent: String, prompt: String, key: Character) -> URLRequest {
        var r = URLRequest(url: URL(string: "http://127.0.0.1:\(port)/api/answer")!)
        r.httpMethod = "POST"
        r.timeoutInterval = 5
        r.setValue("application/json", forHTTPHeaderField: "Content-Type")
        r.setValue("1", forHTTPHeaderField: "X-Cube-Client")
        r.httpBody = try? JSONSerialization.data(
            withJSONObject: ["agent": agent, "prompt": prompt, "key": String(key)], options: [.sortedKeys])
        return r
    }

    /// `done(status, nil)` when any HTTP response arrives, `done(nil, reason)` on a transport error.
    /// Sent once, never retried. May be called on any queue.
    public func answer(agent: String, prompt: String, key: Character, _ done: @escaping (Int?, String?) -> Void) {
        let req = BridgeClient.answerRequest(port: port, agent: agent, prompt: prompt, key: key)
        // A fresh session per answer: URLSession may silently resend a request on a kept-alive
        // connection the server closed, and an answer types keys, so it must reach the bridge once.
        let cfg = URLSessionConfiguration.ephemeral
        cfg.timeoutIntervalForRequest = 5
        cfg.waitsForConnectivity = false
        let once = URLSession(configuration: cfg)
        defer { once.finishTasksAndInvalidate() }
        once.dataTask(with: req) { _, resp, err in
            if let http = resp as? HTTPURLResponse { return done(http.statusCode, nil) }
            done(nil, err?.localizedDescription ?? "no HTTP response")
        }.resume()
    }
}

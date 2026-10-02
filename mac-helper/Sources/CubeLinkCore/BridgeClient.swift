import Foundation

/// Reads the bridge on localhost. The body is passed through byte for byte:
/// the helper never re-encodes the payload, so it cannot drift from the contract.
public final class BridgeClient {
    private let base: URL
    private let session: URLSession

    public init(port: Int) {
        base = URL(string: "http://127.0.0.1:\(port)")!
        let cfg = URLSessionConfiguration.ephemeral
        cfg.timeoutIntervalForRequest = 3
        cfg.waitsForConnectivity = false
        session = URLSession(configuration: cfg)
    }

    /// `done(body, nil)` on success, `done(nil, reason)` otherwise. May be called on any queue.
    public func fetch(_ done: @escaping (Data?, String?) -> Void) {
        session.dataTask(with: base.appendingPathComponent("api/status")) { data, resp, err in
            if let err { return done(nil, err.localizedDescription) }
            guard let http = resp as? HTTPURLResponse else { return done(nil, "no HTTP response") }
            guard http.statusCode == 200 else { return done(nil, "HTTP \(http.statusCode)") }
            let body = data ?? Data()
            if let why = validatePayloadBody(body) { return done(nil, why) }
            done(body, nil)
        }.resume()
    }

    /// Whether something answers /health on the port.
    public func healthy(_ done: @escaping (Bool) -> Void) {
        session.dataTask(with: base.appendingPathComponent("health")) { _, resp, _ in
            done((resp as? HTTPURLResponse)?.statusCode == 200)
        }.resume()
    }
}

import Foundation

/// What the app does after a `GET /api/changes` returns: the bridge answers 204 as soon as its agents
/// change and 304 when its 8 s wait runs out, so the app fetches `/api/status` within a moment of a
/// change instead of at its next poll (`PollPolicy`, which keeps running as the fallback).
public enum ChangesWatch {
    public struct Next: Equatable {
        public let fetch: Bool
        public let delay: TimeInterval

        public init(fetch: Bool, delay: TimeInterval) {
            self.fetch = fetch
            self.delay = delay
        }
    }

    /// Before asking again after anything but 204 / 304: an older bridge (404), a refusal, or none.
    public static let backoff: TimeInterval = 10

    public static func after(status: Int?) -> Next {
        switch status {
        case 204: return Next(fetch: true, delay: 0)
        case 304: return Next(fetch: false, delay: 0)
        default: return Next(fetch: false, delay: backoff)
        }
    }
}

private let changesSession: URLSession = {
    let cfg = URLSessionConfiguration.ephemeral
    cfg.timeoutIntervalForRequest = 12
    cfg.waitsForConnectivity = false
    return URLSession(configuration: cfg)
}()

extension BridgeClient {
    /// `GET /api/changes`, with the loopback guard's header; its timeout outlasts the bridge's 8 s wait.
    public static func changesRequest(port: Int) -> URLRequest {
        var r = URLRequest(url: URL(string: "http://127.0.0.1:\(port)/api/changes")!)
        r.httpMethod = "GET"
        r.timeoutInterval = 12
        r.setValue("1", forHTTPHeaderField: "X-Cube-Client")
        return r
    }

    /// `done(status)` for any HTTP response, `done(nil)` on a transport error. May be called on any queue.
    /// Its own session: the fetch session's 3 s timeout would cut the wait short.
    public func waitForChange(_ done: @escaping (Int?) -> Void) {
        changesSession.dataTask(with: BridgeClient.changesRequest(port: port)) { _, resp, _ in
            done((resp as? HTTPURLResponse)?.statusCode)
        }.resume()
    }
}

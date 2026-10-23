import Foundation

public protocol NavServer: Sendable {
    func route(from: Coord, to: Coord) async throws -> Route
    /// 404 (no streets in the tile) answers `[]`.
    func tile(_ id: TileID) async throws -> [StreetLine]
}

public enum StartError: Error, Equatable { case noSignal, badKey, server(Int) }

/// Backoff before each retry of a 429/503 answer.
let rateLimitBackoff: [TimeInterval] = [0.5, 1, 2, 4]

public final class HTTPNavServer: NavServer {
    private let baseURL: URL
    private let key: String
    private let session: URLSession
    private let sleep: @Sendable (TimeInterval) async throws -> Void

    public init(baseURL: URL, key: String, session: URLSession = .shared,
                sleep: @escaping @Sendable (TimeInterval) async throws -> Void = { try await Task.sleep(nanoseconds: UInt64($0 * 1e9)) }) {
        self.baseURL = baseURL; self.key = key; self.session = session; self.sleep = sleep
    }

    public func route(from: Coord, to: Coord) async throws -> Route {
        var req = URLRequest(url: baseURL.appendingPathComponent("route"))
        req.httpMethod = "POST"
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        let body = "{\"points\":[[\(from.lon),\(from.lat)],[\(to.lon),\(to.lat)]],\"profile\":\"moto_vn\","
            + "\"instructions\":true,\"points_encoded\":false,\"locale\":\"en\",\"details\":[\"road_class\"]}"
        req.httpBody = Data(body.utf8)
        let (data, code) = try await send(req)
        guard code == 200 else { throw StartError.server(code) }
        do { return try RouteDecoding.decode(data) } catch { throw StartError.server(0) }
    }

    public func tile(_ id: TileID) async throws -> [StreetLine] {
        var req = URLRequest(url: baseURL.appendingPathComponent("tiles/15/\(id.x)/\(id.y).json"))
        req.httpMethod = "GET"
        let (data, code) = try await send(req)
        if code == 404 { return [] }
        guard code == 200 else { throw StartError.server(code) }
        do { return try TileDecoding.decode(data) } catch { throw StartError.server(0) }
    }

    /// Sends with the key; retries 429/503 with backoff; maps failures to `StartError`.
    private func send(_ request: URLRequest) async throws -> (Data, Int) {
        var req = request
        req.setValue(key, forHTTPHeaderField: "X-Nav-Key")
        var attempt = 0
        while true {
            let data: Data, code: Int
            do {
                let (d, resp) = try await session.data(for: req)
                data = d; code = (resp as? HTTPURLResponse)?.statusCode ?? 0
            } catch is URLError {
                throw StartError.noSignal
            }
            if code == 401 { throw StartError.badKey }
            if (code == 429 || code == 503), attempt < rateLimitBackoff.count {
                try await sleep(rateLimitBackoff[attempt]); attempt += 1
                continue
            }
            return (data, code)
        }
    }
}

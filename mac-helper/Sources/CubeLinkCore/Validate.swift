import Foundation

/// nil when `body` is something the cube can use; otherwise the reason it is not.
/// Guards against pushing a proxy's HTML error page or an oversized payload.
public func validatePayloadBody(_ body: Data) -> String? {
    if body.isEmpty { return "empty response" }
    if body.count > CubeProtocol.maxPayload {
        return "payload is \(body.count) bytes, over the \(CubeProtocol.maxPayload) byte limit"
    }
    guard let obj = try? JSONSerialization.jsonObject(with: body), obj is [String: Any] else {
        return "response is not a JSON object"
    }
    return nil
}

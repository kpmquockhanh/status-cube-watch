import Foundation
import Testing
@testable import CubeLinkCore

@Test func changesRequestShape() {
    let r = BridgeClient.changesRequest(port: 8787)
    #expect(r.url?.absoluteString == "http://127.0.0.1:8787/api/changes")
    #expect(r.httpMethod == "GET")
    #expect(r.value(forHTTPHeaderField: "X-Cube-Client") == "1")
    // Longer than the bridge's 8 s wait, inside its 10 s write timeout.
    #expect(r.timeoutInterval > 8 && r.timeoutInterval <= 15)
}

@Test func changesWatchPolicy() {
    // 204: the agents changed, fetch now and wait again at once.
    #expect(ChangesWatch.after(status: 204) == .init(fetch: true, delay: 0))
    // 304: nothing changed in the wait, wait again at once.
    #expect(ChangesWatch.after(status: 304) == .init(fetch: false, delay: 0))
    // An older bridge (404), a refusal, or no bridge at all: back off, the 5 s poll still runs.
    #expect(ChangesWatch.after(status: 404) == .init(fetch: false, delay: ChangesWatch.backoff))
    #expect(ChangesWatch.after(status: 500) == .init(fetch: false, delay: ChangesWatch.backoff))
    #expect(ChangesWatch.after(status: nil) == .init(fetch: false, delay: ChangesWatch.backoff))
    #expect(ChangesWatch.backoff >= 5)
}

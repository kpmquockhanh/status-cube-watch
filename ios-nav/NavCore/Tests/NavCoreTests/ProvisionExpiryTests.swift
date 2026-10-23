import Testing
import Foundation
@testable import NavCore

@Test func provisionExpiryReadsTheEmbeddedPlist() throws {
    let url = try #require(Bundle.module.url(forResource: "embedded", withExtension: "mobileprovision", subdirectory: "Fixtures"))
    let date = try #require(provisionExpiry(try Data(contentsOf: url)))
    #expect(date == ISO8601DateFormatter().date(from: "2026-10-29T12:00:00Z"))
}

@Test func provisionExpiryOfGarbageIsNil() {
    #expect(provisionExpiry(Data("nothing here".utf8)) == nil)
}

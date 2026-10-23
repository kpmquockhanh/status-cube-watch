import Foundation
import Testing
@testable import NavCore

@Suite struct FixtureTests {
    @Test func routeFixtureIsBundled() throws {
        let url = try #require(Bundle.module.url(forResource: "route-hcmc", withExtension: "json", subdirectory: "Fixtures"))
        let data = try Data(contentsOf: url)
        #expect(!data.isEmpty)
    }
}

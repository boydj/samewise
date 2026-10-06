import SamewiseKit
import XCTest

@testable import Samewise

/// The app target's own wiring. Everything else is tested in SamewiseKit
/// (swift test) and through the UI tests.
@MainActor
final class SamewiseTests: XCTestCase {
    func testTheSimulatorRunsOnTheFakeRadio() async {
        let options = LaunchOptions(arguments: ProcessInfo.processInfo.arguments, simulator: true)
        XCTAssertTrue(options.makeLink() is FakeRadioLink)
    }
}

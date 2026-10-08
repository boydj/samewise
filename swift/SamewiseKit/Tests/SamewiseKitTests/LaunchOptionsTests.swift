import XCTest

@testable import SamewiseKit

@MainActor
final class LaunchOptionsTests: XCTestCase {
    func testSimulatorAlwaysUsesTheFake() async {
        let o = LaunchOptions(arguments: ["Samewise"], simulator: true)
        XCTAssertTrue(o.fake)
        XCTAssertTrue(o.makeLink() is FakeRadioLink)
    }

    func testFakeArguments() async {
        let o = LaunchOptions(arguments: ["Samewise", "-fakeRadio", "-fakeWindow", "connect", "-fakeBonded"],
                              simulator: false)
        XCTAssertTrue(o.fake)
        XCTAssertEqual(o.window, .connect)
        XCTAssertTrue(o.bonded)
        let link = o.makeLink() as! FakeRadioLink
        XCTAssertEqual(link.window, .connect)
        XCTAssertTrue(link.radioHasBond)
    }

    func testDeviceDefaultsToTheRealRadio() async {
        let o = LaunchOptions(arguments: ["Samewise"], simulator: false)
        XCTAssertFalse(o.fake)
        XCTAssertEqual(o.window, .pairing)
    }
}

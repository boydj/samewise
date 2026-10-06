import XCTest

final class ConnectUITests: XCTestCase {
    override func setUp() {
        continueAfterFailure = false
    }

    func testConnectsToTheFakeRadio() {
        let app = XCUIApplication()
        app.launchArguments = ["-fakeRadio"]
        app.launch()
        app.buttons["connect.start"].tap()
        XCTAssertTrue(app.element("home.battery").waitForExistence(timeout: 10))
    }
}

extension XCUIApplication {
    /// Any element with this accessibility identifier.
    func element(_ id: String) -> XCUIElement {
        descendants(matching: .any).matching(identifier: id).firstMatch
    }
}

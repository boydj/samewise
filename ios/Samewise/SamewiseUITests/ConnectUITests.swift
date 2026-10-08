import XCTest

final class ConnectUITests: XCTestCase {
    override func setUp() {
        continueAfterFailure = false
    }

    func launch(_ arguments: [String]) -> XCUIApplication {
        let app = XCUIApplication()
        app.launchArguments = ["-fakeRadio", "-pairedBefore", "NO"] + arguments
        app.launch()
        return app
    }

    func testFirstRunPairs() {
        let app = launch([])
        XCTAssertTrue(app.element("connect.step1").label.contains("BAND and STBY"))
        app.buttons["connect.start"].tap()
        XCTAssertTrue(app.element("home.battery").waitForExistence(timeout: 10))
    }

    func testWaitsForTheRadiosWindow() {
        let app = launch(["-fakeWindow", "none"])
        app.buttons["connect.start"].tap()
        XCTAssertTrue(app.buttons["connect.stop"].waitForExistence(timeout: 5))
        XCTAssertFalse(app.element("home.battery").exists)
        app.buttons["fake.open"].tap()
        app.buttons["fake.pairingWindow"].tap()
        app.buttons["fake.done"].tap()
        XCTAssertTrue(app.element("home.battery").waitForExistence(timeout: 10))
    }

    func testStaleBondExplainsHowToPairAgain() {
        let app = launch(["-fakeBonded"])
        app.buttons["connect.start"].tap()
        XCTAssertTrue(app.element("home.battery").waitForExistence(timeout: 10))
        app.buttons["fake.open"].tap()
        app.buttons["fake.forgetPhone"].tap()
        app.buttons["fake.drop"].tap()
        app.buttons["fake.pairingWindow"].tap()
        app.buttons["fake.done"].tap()
        app.buttons["connect.start"].tap()
        let step = app.element("connect.step3")
        XCTAssertTrue(step.waitForExistence(timeout: 5))
        XCTAssertTrue(step.label.contains("Forget This Device"))
    }
}

extension XCUIApplication {
    /// Any element with this accessibility identifier.
    func element(_ id: String) -> XCUIElement {
        descendants(matching: .any).matching(identifier: id).firstMatch
    }
}

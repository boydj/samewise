import XCTest

/// The main screens against the fake radio.
final class RadioUITests: XCTestCase {
    var app: XCUIApplication!

    override func setUp() {
        continueAfterFailure = false
        app = XCUIApplication()
        app.launchArguments = ["-fakeRadio", "-pairedBefore", "YES"]
        app.launch()
        app.buttons["connect.start"].tap()
        XCTAssertTrue(app.element("home.battery").waitForExistence(timeout: 10))
    }

    func fakeRadio(_ buttons: String...) {
        app.buttons["fake.open"].tap()
        for b in buttons {
            app.buttons[b].tap()
        }
        app.buttons["fake.done"].tap()
    }

    func testFactoryResetWaitsForConfirmationAndShowsTheResult() {
        app.buttons["control.reset"].tap()
        app.buttons["control.resetConfirm"].firstMatch.tap()  // the dialog nests the button
        let result = app.element("control.result")
        XCTAssertTrue(result.waitForExistence(timeout: 5))
        XCTAssertTrue(result.label.contains("long-press STBY"), result.label)

        fakeRadio("fake.confirm")
        // The radio unpairs every phone: back to pairing, with the way to do it.
        let step = app.element("connect.step3")
        XCTAssertTrue(step.waitForExistence(timeout: 5))
        XCTAssertTrue(step.label.contains("Forget This Device"))
    }

    func testCancelledFactoryReset() {
        app.buttons["control.reset"].tap()
        app.buttons["control.resetConfirm"].firstMatch.tap()  // the dialog nests the button
        XCTAssertTrue(app.element("control.result").waitForExistence(timeout: 5))
        fakeRadio("fake.cancel")
        XCTAssertTrue(app.element("control.result").label.contains("cancelled"))
        XCTAssertTrue(app.element("home.battery").exists)
    }

    func testStatusAndAlertNotificationsUpdateTheScreens() {
        fakeRadio("fake.battery", "fake.alert")
        XCTAssertTrue(app.element("home.battery").label.contains("15%"), app.element("home.battery").label)
        app.tabBars.buttons["Alert log"].tap()
        let row = app.element("log.row0")
        XCTAssertTrue(row.waitForExistence(timeout: 5))
        XCTAssertTrue(row.label.contains("Tornado Warning"), row.label)
    }

    func testTestAlertIsLogged() {
        app.buttons["control.test"].tap()
        XCTAssertTrue(app.element("control.result").waitForExistence(timeout: 5))
        app.tabBars.buttons["Alert log"].tap()
        XCTAssertTrue(app.element("log.row0").label.contains("Test alert"))
    }
}

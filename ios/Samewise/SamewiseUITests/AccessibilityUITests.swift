import XCTest

/// Dynamic Type and VoiceOver: Xcode's accessibility audit on every screen,
/// and the screens at the largest accessibility text size.
final class AccessibilityUITests: XCTestCase {
    override func setUp() {
        continueAfterFailure = false
    }

    func launch(textSize: String? = nil) -> XCUIApplication {
        let app = XCUIApplication()
        app.launchArguments = ["-fakeRadio", "-pairedBefore", "NO"]
        if let textSize {
            app.launchArguments += ["-UIPreferredContentSizeCategoryName", textSize]
        }
        app.launch()
        return app
    }

    func connect(_ app: XCUIApplication) {
        app.buttons["connect.start"].tap()
        XCTAssertTrue(app.element("home.battery").waitForExistence(timeout: 10))
    }

    func testEveryScreenPassesTheAccessibilityAudit() throws {
        let app = launch()
        try app.performAccessibilityAudit()
        connect(app)
        try app.performAccessibilityAudit()
        app.tabBars.buttons["Settings"].tap()
        try app.performAccessibilityAudit()
        app.tabBars.buttons["Alert log"].tap()
        try app.performAccessibilityAudit()
    }

    func testLargestTextStillWorks() {
        let app = launch(textSize: "UICTContentSizeCategoryAccessibilityXXXL")
        XCTAssertTrue(app.buttons["connect.start"].isHittable)
        connect(app)
        app.swipeUp()
        XCTAssertTrue(app.buttons["control.test"].waitForExistence(timeout: 5))
        app.tabBars.buttons["Settings"].tap()
        XCTAssertTrue(app.element("settings.homeCounties").waitForExistence(timeout: 5))
    }
}

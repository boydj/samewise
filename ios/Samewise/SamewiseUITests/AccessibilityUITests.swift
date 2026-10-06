import XCTest

/// Dynamic Type and VoiceOver: Xcode's accessibility audit on every screen,
/// and the screens at the largest accessibility text size.
final class AccessibilityUITests: XCTestCase {
    override func setUp() {
        continueAfterFailure = true
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
        XCTAssertTrue(app.element("home.battery").waitForExistence(timeout: 10), "connected")
    }

    /// Runs the audit and fails once per screen with every issue spelled out.
    func audit(_ app: XCUIApplication, _ screen: String) throws {
        var issues: [String] = []
        try app.performAccessibilityAudit { issue in
            let element = issue.element.map { "\($0.elementType.rawValue) '\($0.label)' id '\($0.identifier)'" }
            issues.append("\(issue.compactDescription): \(element ?? "no element")")
            return true
        }
        XCTAssertEqual(issues, [], "\(screen): " + issues.joined(separator: "; "))
    }

    func testEveryScreenPassesTheAccessibilityAudit() throws {
        let app = launch()
        try audit(app, "Connect")
        connect(app)
        try audit(app, "Radio")
        app.tabBars.buttons["Settings"].tap()
        try audit(app, "Settings")
        app.tabBars.buttons["Alert log"].tap()
        try audit(app, "Alert log")
    }

    func testLargestTextStillWorks() {
        let app = launch(textSize: "UICTContentSizeCategoryAccessibilityXXXL")
        let start = app.buttons["connect.start"]
        XCTAssertTrue(app.scrollTo(start), "Pair button reachable")
        start.tap()
        XCTAssertTrue(app.element("home.battery").waitForExistence(timeout: 10), "connected")
        XCTAssertTrue(app.scrollTo(app.buttons["control.test"]), "Test alert reachable")
        app.tabBars.buttons["Settings"].tap()
        XCTAssertTrue(app.scrollTo(app.element("settings.homeCounties")), "Choose counties reachable")
    }
}

extension XCUIApplication {
    /// Swipes up until the element is on screen; false if it never is.
    func scrollTo(_ e: XCUIElement, swipes: Int = 10) -> Bool {
        for _ in 0..<swipes {
            if e.exists && e.isHittable { return true }
            swipeUp()
        }
        return e.exists && e.isHittable
    }
}

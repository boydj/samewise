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

    /// Audits a screen a screenful at a time: each element is judged while
    /// it is fully visible between the navigation bar and the tab bar, then
    /// the screen scrolls on. (Elements cut off by the screen edge or behind
    /// a translucent bar otherwise read as clipped and low-contrast.) Fails
    /// once per screen with every issue spelled out.
    ///
    /// One exception: iOS caps the text size of navigation bar buttons, so
    /// their Dynamic Type finding is the system's, not the app's.
    func audit(_ app: XCUIApplication, _ screen: String, pages: Int = 4) throws {
        var issues: [String] = []
        let barButtons = Set(app.navigationBars.buttons.allElementsBoundByIndex.map(\.identifier).filter { !$0.isEmpty })
        for page in 0..<pages {
            let window = app.windows.firstMatch.frame
            let top = app.navigationBars.firstMatch.exists ? app.navigationBars.firstMatch.frame.maxY : window.minY
            let bottom = app.tabBars.firstMatch.exists ? app.tabBars.firstMatch.frame.minY : window.maxY
            let visible = CGRect(x: window.minX, y: top, width: window.width, height: bottom - top)
            try app.performAccessibilityAudit { issue in
                if let e = issue.element {
                    if issue.auditType == .dynamicType, barButtons.contains(e.identifier) { return true }
                    if !visible.contains(e.frame) { return true }  // judged on the page where it's whole
                }
                let element = issue.element.map { "\($0.elementType.rawValue) '\($0.label)' id '\($0.identifier)'" }
                let text = "\(issue.compactDescription): \(element ?? "no element")"
                if !issues.contains(text) {
                    issues.append(text)
                }
                return true
            }
            if page < pages - 1 {
                app.swipeUp()
            }
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

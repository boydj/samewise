import XCTest

/// Counties from the bundled Census table, against the fake radio.
final class CountyUITests: XCTestCase {
    override func setUp() {
        continueAfterFailure = false
    }

    func testFirstRunPairChooseCountiesSeeThemOnHome() {
        let app = XCUIApplication()
        app.launchArguments = ["-fakeRadio", "-pairedBefore", "NO"]
        app.launch()
        app.buttons["connect.start"].tap()
        XCTAssertTrue(app.element("home.noCounties").waitForExistence(timeout: 10))

        app.tabBars.buttons["Settings"].tap()
        app.element("settings.homeCounties").tap()
        let search = app.searchFields.firstMatch
        XCTAssertTrue(search.waitForExistence(timeout: 5))
        search.tap()
        search.typeText("Travis, Texas")
        app.buttons["counties.result.048453"].tap()
        search.typeText("")
        app.buttons["Cancel"].firstMatch.tapIfExists()
        app.buttons["counties.save"].tap()

        app.tabBars.buttons["Radio"].tap()
        let county = app.element("home.county.048453")
        XCTAssertTrue(county.waitForExistence(timeout: 5))
        XCTAssertTrue(county.label.contains("Travis County, TX"), county.label)
    }

    func testTravelModeWithAMockedLocationWritesTheTravelCounty() {
        let app = XCUIApplication()
        app.launchArguments = ["-fakeRadio", "-pairedBefore", "YES", "-fakeLocation", "Bexar County|San Antonio|TX"]
        app.launch()
        app.buttons["connect.start"].tap()
        XCTAssertTrue(app.element("home.battery").waitForExistence(timeout: 10))
        app.tabBars.buttons["Settings"].tap()
        app.element("travel.toggle").switches.firstMatch.tapIfExists(or: app.element("travel.toggle"))
        let county = app.element("travel.county.048029")
        XCTAssertTrue(county.waitForExistence(timeout: 5), "turning travel mode on looks the county up")
        XCTAssertTrue(county.label.contains("Bexar County, TX"), county.label)
    }
}

extension XCUIElement {
    func tapIfExists(or fallback: XCUIElement? = nil) {
        if exists {
            tap()
        } else {
            fallback?.tap()
        }
    }
}

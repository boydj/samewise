import GattModel
import XCTest

@testable import SamewiseKit

@MainActor
final class ControlTests: XCTestCase {
    var link: FakeRadioLink!
    var session: RadioSession!

    override func setUp() async throws {
        link = FakeRadioLink()
        session = RadioSession(link: link)
        session.connect()
        await session.settled()
    }

    func testTestAlertIsLoggedAndShown() async {
        let ok = await session.send(.testAlert)
        XCTAssertTrue(ok)
        XCTAssertEqual(session.lastControl, .init(command: .testAlert, result: .done))
        XCTAssertEqual(session.log.first?.outcome, RadioState.outcomeTest)
        XCTAssertTrue(session.lastControl!.text.contains("test alert"))
    }

    func testClearLog() async {
        link.radio.injectAlert()
        await session.send(.clearLog)
        XCTAssertEqual(session.lastControl?.result, .done)
        XCTAssertTrue(link.radio.log.isEmpty)
    }

    func testFactoryResetWaitsForTheRadio() async {
        await session.setHomeCounties(["048453"])
        await session.send(.factoryReset)
        XCTAssertEqual(session.lastControl, .init(command: .factoryReset, result: .awaitingConfirmation))
        XCTAssertTrue(session.lastControl!.text.contains("long-press STBY"))
        XCTAssertEqual(link.radio.home, ["048453"], "nothing happens until it's confirmed")

        let busy = await session.send(.clearLog)
        XCTAssertFalse(busy)
        XCTAssertEqual(session.problem, .att(AttError.busy.code))
        XCTAssertTrue(session.problem!.message.contains("Long-press STBY"))

        link.radio.cancel()
        XCTAssertEqual(session.lastControl, .init(command: .factoryReset, result: .cancelled))
        XCTAssertEqual(session.linkState, .connected)
    }

    func testConfirmedFactoryResetUnpairsAndExplains() async {
        await session.setHomeCounties(["048453"])
        await session.send(.factoryReset)
        link.radio.confirm()
        XCTAssertEqual(link.radio.home, [])
        XCTAssertEqual(session.linkState, .idle, "the radio unpairs every phone")
        XCTAssertFalse(link.radioHasBond)
        XCTAssertEqual(session.problem, .staleBond)
        XCTAssertEqual(ConnectAdvice.make(state: .idle, problem: session.problem, pairedBefore: true), .staleBond)
    }
}

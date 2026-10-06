import GattModel
import XCTest

@testable import SamewiseKit

@MainActor
final class MessagesTests: XCTestCase {
    /// Every ATT error in docs/gatt.json has its own plain-language text.
    func testEveryGattJsonErrorHasText() async {
        var seen: Set<String> = []
        for e in GattDocument.bundled.errors {
            let text = RadioError.att(UInt8(e.code)).message
            XCTAssertFalse(text.contains("error \(e.code)"), "\(e.name) falls through to the generic text")
            XCTAssertFalse(text.isEmpty)
            seen.insert(text)
        }
        XCTAssertGreaterThanOrEqual(seen.count, GattDocument.bundled.errors.count - 1, "LENGTH and SCHEMA may share")
        XCTAssertTrue(RadioError.att(0x7F).message.contains("error 127"))
    }

    func testFirstTimeAndReconnectAdvice() async {
        XCTAssertEqual(ConnectAdvice.make(state: .idle, problem: nil, pairedBefore: false), .start(firstTime: true))
        XCTAssertEqual(ConnectAdvice.start(firstTime: true).action, "Pair")
        XCTAssertTrue(ConnectAdvice.openWindow(firstTime: true).steps[0].contains("BAND and STBY"))
        XCTAssertEqual(ConnectAdvice.make(state: .searching, problem: nil, pairedBefore: true),
                       .openWindow(firstTime: false))
        XCTAssertTrue(ConnectAdvice.openWindow(firstTime: false).steps[0].contains("hold BAND"))
        XCTAssertNil(ConnectAdvice.openWindow(firstTime: false).action)
    }

    func testStaleBondExplainsForgetting() async {
        let a = ConnectAdvice.make(state: .idle, problem: .staleBond, pairedBefore: true)
        XCTAssertEqual(a, .staleBond)
        XCTAssertTrue(a.steps.joined().contains("Forget This Device"))
        XCTAssertEqual(a.action, "Pair")
    }

    func testBluetoothAndLostConnection() async {
        XCTAssertEqual(ConnectAdvice.make(state: .idle, problem: .bluetooth(.off), pairedBefore: true),
                       .bluetooth(.off))
        XCTAssertEqual(ConnectAdvice.make(state: .idle, problem: .disconnected, pairedBefore: true), .lostConnection)
        XCTAssertEqual(ConnectAdvice.make(state: .searching, problem: .disconnected, pairedBefore: true),
                       .openWindow(firstTime: false), "searching again replaces the old problem")
    }
}

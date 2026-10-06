import GattModel
import XCTest

@testable import SamewiseKit

@MainActor
final class RadioSessionTests: XCTestCase {
    var link: FakeRadioLink!
    var session: RadioSession!

    override func setUp() async throws {
        link = FakeRadioLink()
        session = RadioSession(link: link)
    }

    func connected() async {
        session.connect()
        await session.settled()
    }

    // MARK: - The link

    func testCharacteristicsMatchGattJson() async {
        XCTAssertEqual(Chr.allCases.map(\.rawValue), GattDocument.bundled.characteristics.map(\.name))
        for chr in Chr.subscribed {
            let c = GattDocument.bundled.characteristic(chr.rawValue)!
            XCTAssertTrue(c.notifies || c.indicates, chr.rawValue)
        }
    }

    func testConnectsAndReadsEverything() async {
        await connected()
        XCTAssertEqual(session.linkState, .connected)
        XCTAssertTrue(session.loaded)
        XCTAssertNil(session.problem)
        XCTAssertEqual(session.events.count, GattDocument.bundled.defaultEventTable.entries.count)
        XCTAssertEqual(session.events.first?.code, GattDocument.bundled.defaultEventTable.entries[0].code)
        XCTAssertEqual(session.log, [], "an empty log reads as empty")
        XCTAssertEqual(session.status?.batteryPercent, 100)
    }

    func testReadsTheWholeLogNewestFirst() async {
        link.radio.injectAlert(event: "TOR")
        link.radio.injectAlert(event: "SVR")
        await connected()
        XCTAssertEqual(session.log.count, 2)
        XCTAssertTrue(session.log[0].raw.contains("-SVR-"))
        XCTAssertTrue(session.log[1].raw.contains("-TOR-"))
    }

    func testDisconnectAndReconnect() async {
        await connected()
        session.disconnect()
        XCTAssertEqual(session.linkState, .idle)
        XCTAssertFalse(session.loaded)
        link.window = .connect
        await connected()
        XCTAssertEqual(session.linkState, .connected)
        XCTAssertTrue(session.loaded)
    }

    func testNotConnectedWritesFail() async {
        let ok = await session.setHomeCounties(["048453"])
        XCTAssertFalse(ok)
        XCTAssertEqual(session.problem, .notConnected)
    }

    // MARK: - Writes round-trip and show the radio's values

    func testSettingsRoundTrip() async {
        await connected()
        var ok = await session.setHomeCounties(["048453", "048029"])
        XCTAssertTrue(ok)
        ok = await session.setTravelCounties(["006037"])
        XCTAssertTrue(ok)
        ok = await session.setMode(travel: true, channel: 3)
        XCTAssertTrue(ok)
        var bitmap = [UInt8](repeating: 0, count: Codec.filterBytes)
        bitmap[0] = 0b101
        ok = await session.setFilter(preset: 3, bitmap: bitmap)
        XCTAssertTrue(ok)
        ok = await session.setPresets([Codec.Preset(band: 0, khz: 101_100), Codec.Preset(band: 2, khz: 162_550)])
        XCTAssertTrue(ok)

        XCTAssertEqual(link.radio.home, ["048453", "048029"])
        XCTAssertEqual(session.settings.home, ["048453", "048029"])
        XCTAssertEqual(session.settings.travel, ["006037"])
        XCTAssertTrue(session.settings.travelMode)
        XCTAssertEqual(session.settings.channel, 3)
        XCTAssertEqual(session.settings.filterPreset, 3)
        XCTAssertEqual(session.settings.filterBitmap, bitmap)
        XCTAssertEqual(session.settings.presets.count, 2)
        XCTAssertNil(session.problem)
    }

    func testRejectedWriteShowsTheErrorAndTheRadiosValues() async {
        await connected()
        await session.setHomeCounties(["048453"])
        let ok = await session.setHomeCounties(["04845X"])
        XCTAssertFalse(ok)
        XCTAssertEqual(session.problem, .att(AttError.value.code))
        XCTAssertEqual(session.settings.home, ["048453"], "the radio's value, not the attempt")

        await session.setPresets([Codec.Preset(band: 2, khz: 162_410)])  // off the 25 kHz grid
        XCTAssertEqual(session.problem, .att(AttError.value.code))
        XCTAssertEqual(session.settings.presets, [])
        await session.setHomeCounties(["048029"])
        XCTAssertNil(session.problem, "a success clears it")
    }

    // MARK: - Notifications

    func testStatusAndLogNotificationsUpdateTheSession() async {
        await connected()
        link.radio.setBattery(42)
        XCTAssertEqual(session.status?.batteryPercent, 42)
        link.radio.setSignal(snr: 10, rssi: 20)
        XCTAssertEqual(session.status?.snrDb, 10)
        link.radio.injectAlert(event: "FFW")
        XCTAssertEqual(session.log.count, 1)
        XCTAssertTrue(session.log[0].raw.contains("-FFW-"))
        link.radio.injectAlert(event: "TOR")
        XCTAssertTrue(session.log[0].raw.contains("-TOR-"))
        XCTAssertEqual(session.log.count, 2)
    }

    func testNotificationsStopWhenDisconnected() async {
        await connected()
        session.disconnect()
        link.radio.setBattery(10)
        XCTAssertEqual(session.status?.batteryPercent, 100)
    }

    // MARK: - Connection policy (the fake follows the radio's)

    func testNoWindowNoConnection() async {
        link.window = .none
        session.connect()
        XCTAssertEqual(session.linkState, .searching)
        link.window = .pairing
        await session.settled()
        XCTAssertEqual(session.linkState, .connected)
    }

    func testConnectWindowIsForBondedPhonesOnly() async {
        link.window = .connect
        session.connect()
        XCTAssertEqual(session.linkState, .searching, "a new phone can't use the connect window")
        link.window = .pairing
        XCTAssertEqual(session.linkState, .connected)
        XCTAssertTrue(link.radioHasBond)
        XCTAssertTrue(link.phoneHasBond)
    }

    func testStaleBond() async {
        await connected()
        session.disconnect()
        link.radioForgetsPhone()
        link.window = .pairing
        session.connect()
        XCTAssertEqual(session.problem, .staleBond)
        XCTAssertEqual(session.linkState, .idle)
        link.phoneForgetsRadio()
        link.window = .pairing
        await connected()
        XCTAssertEqual(session.linkState, .connected)
        XCTAssertNil(session.problem)
    }

    func testPairingDeclined() async {
        link.acceptsPairing = false
        session.connect()
        XCTAssertEqual(session.problem, .pairingFailed)
        XCTAssertEqual(session.linkState, .idle)
    }

    func testDroppedLink() async {
        await connected()
        link.dropLink()
        XCTAssertEqual(session.linkState, .idle)
        XCTAssertEqual(session.problem, .disconnected)
    }

    func testOnConnectStepsRunBeforeTheReads() async {
        var order: [String] = []
        session.onConnect = [{ s in order.append("step loaded=\(s.loaded)") }]
        await connected()
        XCTAssertEqual(order, ["step loaded=false"])
        XCTAssertTrue(session.loaded)
    }
}

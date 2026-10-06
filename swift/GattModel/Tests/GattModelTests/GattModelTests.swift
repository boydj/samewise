import XCTest

@testable import GattModel

final class GattModelTests: XCTestCase {
    var document: GattDocument!
    var radio: RadioState!
    var sent: [(String, [UInt8])] = []

    override func setUpWithError() throws {
        document = try GattDocument.load(from: GattDocument.repositoryURL)
        radio = RadioState(document: document)
        sent = []
        radio.send = { [unowned self] name, bytes in self.sent.append((name, bytes)) }
    }

    // MARK: - The mock follows docs/gatt.json

    func testBundledCopyIsDocsGattJson() throws {
        XCTAssertEqual(try Data(contentsOf: GattDocument.bundledURL),
                       try Data(contentsOf: GattDocument.repositoryURL),
                       "run tools/gatt/gatt_json.py")
        XCTAssertEqual(GattDocument.bundled.service.uuid, document.service.uuid)
    }

    func testServesEveryCharacteristic() {
        XCTAssertEqual(Set(document.characteristics.map(\.name)), RadioState.served)
        for c in document.characteristics {
            if c.readable {
                switch radio.read(c.name) {
                case .success(let bytes):
                    XCTAssertLessThanOrEqual(bytes.count, c.maxLength, c.name)
                    XCTAssertEqual(bytes.first, UInt8(c.schema), c.name)
                case .failure(let e):
                    XCTAssertEqual(e, .index, "\(c.name): only an empty list may refuse a read")
                }
            } else {
                XCTAssertEqual(radio.read(c.name), .failure(.readNotPermitted), c.name)
            }
            if !c.writable {
                XCTAssertEqual(radio.write(c.name, [1]), .writeNotPermitted, c.name)
            }
        }
    }

    func testErrorCodesMatchGattJson() {
        for e in AttError.allCases {
            guard let name = e.gattName else { continue }
            XCTAssertEqual(document.errors.first { $0.name == name }?.code, Int(e.code), name)
        }
        XCTAssertEqual(Set(document.errors.map(\.name)),
                       Set(AttError.allCases.compactMap(\.gattName)), "every error in gatt.json")
    }

    func testStartsWithTheFirmwareDefaultEventTable() throws {
        XCTAssertEqual(radio.events.count, document.defaultEventTable.entries.count)
        let read = try radio.read("event_table").get()
        XCTAssertEqual(Array(read[5..<8]), Array(document.defaultEventTable.entries[0].code.utf8))
    }

    func testMaximumValuesMatchMaxLength() {
        let sixteen = (0..<16).map { String(format: "%d48%03d", $0 % 10, $0) }
        XCTAssertNil(radio.write("counties", Codec.encodeCounties(sixteen)))
        XCTAssertEqual(try radio.read("counties").get().count, document.characteristic("counties")!.maxLength)
        XCTAssertEqual(Codec.encodeStatus(Codec.Status()).count, document.characteristic("status")!.maxLength)
        let tz = String(repeating: "A", count: Codec.tzMax)
        XCTAssertEqual(Codec.encodeTime(utc: 0, tz: tz).count, document.characteristic("time")!.maxLength)
    }

    // MARK: - Validation, as on the radio

    func testCountiesRoundTripAndRejections() throws {
        XCTAssertNil(radio.write("counties", Codec.encodeCounties(["048453", "148029"])))
        XCTAssertEqual(try Codec.decodeCounties(radio.read("counties").get()), ["048453", "148029"])

        var bad = Codec.encodeCounties(["048453"])
        bad[0] = 2
        XCTAssertEqual(radio.write("counties", bad), .schema)
        XCTAssertEqual(radio.write("counties", Array(Codec.encodeCounties(["048453"]).dropLast())), .length)
        XCTAssertEqual(radio.write("counties", Codec.encodeCounties(["04845A"])), .value)
        XCTAssertEqual(radio.write("counties", Codec.encodeCounties(["048453"]) + [0]), .length)
        XCTAssertEqual(radio.home, ["048453", "148029"], "unchanged")
    }

    func testModeFilterPresets() throws {
        XCTAssertNil(radio.write("mode", Codec.encodeMode(mode: 1, channel: 3)))
        XCTAssertEqual(radio.write("mode", Codec.encodeMode(mode: 0, channel: 8)), .value)
        XCTAssertEqual(radio.channel, 3)
        XCTAssertEqual(radio.write("event_filter", Codec.encodeFilter(preset: 4, bitmap: [UInt8](repeating: 0, count: 16))), .value)
        XCTAssertNil(radio.write("presets", Codec.encodePresets([Codec.Preset(band: 2, khz: 162475)])))
        XCTAssertEqual(radio.write("presets", Codec.encodePresets([Codec.Preset(band: 2, khz: 162410)])), .value)
        XCTAssertEqual(radio.presets.count, 1)
    }

    func testTimeZones() {
        XCTAssertNil(radio.write("time", Codec.encodeTime(utc: 1_791_172_800, tz: "EST5EDT,M3.2.0,M11.1.0")))
        XCTAssertEqual(radio.utc, 1_791_172_800)
        for tz in ["UTC0", "AEST-10AEDT,M10.1.0,M4.1.0/3", "<+0530>-5:30", "XST8XDT7,J60/0,J305/0"] {
            XCTAssertTrue(PosixTimeZone.isValid(tz), tz)
        }
        for tz in ["EST", "EST5EDT", "EST5EDT,M13.2.0,M11.1.0", "EST25", "EST5 ", "<E>5", "EST5:60"] {
            XCTAssertFalse(PosixTimeZone.isValid(tz), tz)
            XCTAssertEqual(radio.write("time", Codec.encodeTime(utc: 5, tz: tz)), .value, tz)
        }
        XCTAssertEqual(radio.utc, 1_791_172_800, "rejected writes leave the clock")
    }

    func testEventTableStagedWrite() throws {
        let tor = Codec.Event(code: "TOR", eventClass: 0, name: "Tornado Warning")
        let rwt = Codec.Event(code: "RWT", eventClass: 4, name: "Required Weekly Test")
        func w(_ op: Codec.EventTableWrite) -> AttError? { radio.write("event_table", Codec.encodeEventTableWrite(op)) }

        XCTAssertEqual(w(.entry(index: 0, event: tor)), .sequence, "before begin")
        XCTAssertEqual(w(.begin(version: 9, count: 0)), .value, "empty table")
        XCTAssertNil(w(.begin(version: 9, count: 2)))
        XCTAssertEqual(w(.entry(index: 1, event: tor)), .sequence, "out of order")
        XCTAssertNil(w(.entry(index: 0, event: tor)))
        XCTAssertEqual(w(.entry(index: 1, event: tor)), .value, "repeated code")
        XCTAssertEqual(w(.commit), .sequence, "one short")
        XCTAssertNil(w(.entry(index: 1, event: rwt)))
        XCTAssertNil(w(.commit))
        XCTAssertEqual(radio.eventVersion, 9)
        XCTAssertEqual(radio.events, [tor, rwt])
        XCTAssertNil(w(.select(index: 1)))
        XCTAssertEqual(try radio.read("event_table").get()[4], 1)
        XCTAssertEqual(w(.select(index: 2)), .index)
    }

    // MARK: - Notifications and Control

    func testStatusNotifiesOnBatteryAndThreeDecibels() {
        radio.setBattery(57)
        XCTAssertEqual(sent.last?.0, "status")
        let count = sent.count
        radio.setSignal(snr: 23, rssi: 41)
        XCTAssertEqual(sent.count, count, "2 dB: quiet")
        radio.setSignal(snr: 22, rssi: 41)
        XCTAssertEqual(sent.count, count + 1, "3 dB")
        XCTAssertEqual(try Codec.decodeStatus(sent.last!.1).snrDb, 22)
    }

    func testAlertAndTestAlertNotifyTheLog() throws {
        radio.injectAlert()
        XCTAssertEqual(sent.last?.0, "alert_log")
        XCTAssertNil(radio.write("control", Codec.encodeCommand(.testAlert)))
        XCTAssertEqual(sent.last!.1, Codec.encodeControlIndication(.testAlert, .done))
        XCTAssertEqual(radio.log.first?.outcome, RadioState.outcomeTest)
        XCTAssertNil(radio.write("alert_log", [1, 1]))
        let older = try radio.read("alert_log").get()
        XCTAssertTrue(String(decoding: older[14...], as: UTF8.self).hasPrefix("ZCZC-WXR-TOR-048453+0030-"))
        XCTAssertNil(radio.write("alert_log", [1, 2]))
        XCTAssertEqual(radio.read("alert_log"), .failure(.index))
    }

    func testFactoryResetWaitsForConfirmation() {
        XCTAssertNil(radio.write("counties", Codec.encodeCounties(["048453"])))
        XCTAssertNil(radio.write("control", Codec.encodeCommand(.factoryReset)))
        XCTAssertEqual(sent.last!.1, Codec.encodeControlIndication(.factoryReset, .awaitingConfirmation))
        XCTAssertEqual(radio.write("control", Codec.encodeCommand(.clearLog)), .busy)
        radio.cancel()
        XCTAssertEqual(sent.last!.1, Codec.encodeControlIndication(.factoryReset, .cancelled))
        XCTAssertEqual(radio.home, ["048453"])
        XCTAssertNil(radio.write("control", Codec.encodeCommand(.factoryReset)))
        radio.confirm()
        XCTAssertEqual(sent.last!.1, Codec.encodeControlIndication(.factoryReset, .done))
        XCTAssertEqual(radio.home, [])
    }
}

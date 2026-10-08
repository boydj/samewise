import Foundation
import GattModel
import XCTest

@testable import SamewiseKit

@MainActor
final class StatusModelTests: XCTestCase {
    let chicago = TimeZone(identifier: "America/Chicago")!
    let now = Date(timeIntervalSince1970: 1_791_176_400)  // 2026-10-05 05:00 UTC, 00:00 in Chicago

    func testStatusLines() async {
        var s = Codec.Status()
        s.batteryPercent = 57
        s.hoursLeft = 30
        s.channel = 1
        s.lastRwtUtc = 1_791_176_400 - 3600
        s.healthFlags = 0b101
        s.locked = 1
        let sum = StatusSummary(s, now: now, zone: chicago)
        XCTAssertEqual(sum.battery, "57%")
        XCTAssertEqual(sum.hoursLeft, "30 hours")
        XCTAssertEqual(sum.channel, "WX1, 162.550 MHz")
        XCTAssertEqual(sum.lastWeeklyTest, "Yesterday 23:00")
        XCTAssertEqual(sum.warnings.map(\.name), ["NO_SIGNAL", "BATTERY_LOW"])
        XCTAssertTrue(sum.locked)

        s.hoursLeft = 0xFFFF
        s.lastRwtUtc = -1
        s.healthFlags = 0
        let fresh = StatusSummary(s, now: now, zone: chicago)
        XCTAssertEqual(fresh.hoursLeft, "Not known yet")
        XCTAssertEqual(fresh.lastWeeklyTest, "None received yet")
        XCTAssertEqual(fresh.warnings, [])
    }

    func testEveryHealthFlagHasText() async {
        for f in GattDocument.bundled.healthFlags {
            XCTAssertFalse(StatusSummary.warningText(f.name).contains(f.name), f.name)
        }
        for o in GattDocument.bundled.logOutcomes {
            XCTAssertNotEqual(LogRow.outcomeText(o.name), "Logged", o.name)
        }
    }

    func testSameHeader() async throws {
        let h = try XCTUnwrap(SameHeader("ZCZC-WXR-TOR-048453-048029+0030-2781915-KEWX/NWS-"))
        XCTAssertEqual(h.originator, "WXR")
        XCTAssertEqual(h.event, "TOR")
        XCTAssertEqual(h.locations, ["048453", "048029"])
        XCTAssertEqual(h.purgeMinutes, 30)
        XCTAssertEqual([h.issueDay, h.issueHour, h.issueMinute], [278, 19, 15])
        XCTAssertEqual(h.station, "KEWX/NWS")
        XCTAssertEqual(SameHeader("ZCZC-WXR-SVR-048453+0145-2781915-KEWX/NWS-")?.purgeMinutes, 105)
        XCTAssertNil(SameHeader("TEST ALERT"))
        XCTAssertNil(SameHeader("ZCZC-WXR-TOR-048453-2781915-KEWX/NWS-"))
    }

    func testLogRowsUseTheRadiosEventNames() async {
        let events = [Codec.Event(code: "TOR", eventClass: 0, name: "Tornado Warning")]
        let e = Codec.LogEntry(receivedUtc: 1_791_176_400 - 600, outcome: 0, flags: 0,
                               raw: "ZCZC-WXR-TOR-048453+0030-2780450-KEWX/NWS-")
        let row = LogRow(e, events: events, now: now, zone: chicago)
        XCTAssertEqual(row.title, "Tornado Warning")
        XCTAssertEqual(row.outcome, "Alerted")
        XCTAssertEqual(row.received, "Yesterday 23:50", "local time, not UTC")
        XCTAssertEqual(row.locations, ["048453"])

        let unknown = LogRow(Codec.LogEntry(receivedUtc: -1, outcome: 2, flags: 0,
                                            raw: "ZCZC-WXR-ZZZ-048453+0030-2780450-KEWX/NWS-"),
                             events: events, now: now, zone: chicago)
        XCTAssertEqual(unknown.title, "Unknown event (ZZZ)")
        XCTAssertEqual(unknown.received, "Before the clock was set")
        let test = LogRow(Codec.LogEntry(receivedUtc: 1_791_176_400, outcome: 4, flags: 0, raw: "TEST ALERT"),
                          events: events, now: now, zone: chicago)
        XCTAssertEqual(test.title, "Test alert")
        XCTAssertEqual(test.received, "Today 00:00")
    }
}

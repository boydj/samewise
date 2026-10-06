import Foundation
import GattModel
import XCTest

@testable import SamewiseKit

@MainActor
final class TimeZoneRuleTests: XCTestCase {
    static let start = Date(timeIntervalSince1970: 1_767_225_600)  // 2026-01-01 00:00 UTC
    static let end = Date(timeIntervalSince1970: 1_861_920_000)    // 2029-01-01 00:00 UTC

    static let zones = [
        "America/New_York", "America/Chicago", "America/Denver", "America/Los_Angeles",
        "America/Anchorage", "America/Phoenix", "Pacific/Honolulu", "America/Puerto_Rico",
        "Pacific/Guam", "America/St_Johns", "Europe/London", "Europe/Berlin", "Australia/Sydney",
        "Australia/Lord_Howe", "Pacific/Auckland", "America/Santiago", "Asia/Kolkata", "UTC",
    ]

    /// The brief's check: every 15 minutes across 2026-2028, the radio's local
    /// time from the generated string matches TimeZone.
    func testGeneratedStringsMatchTimeZoneEvery15Minutes() async throws {
        for id in Self.zones {
            let zone = try XCTUnwrap(TimeZone(identifier: id), id)
            let rule = TimeZoneRule.make(for: zone, at: Self.start)
            XCTAssertTrue(rule.exact, "\(id): \(rule.tz)")
            XCTAssertLessThanOrEqual(rule.tz.utf8.count, Codec.tzMax, id)
            let info = try XCTUnwrap(PosixTimeZone.parse(rule.tz), "\(id): \(rule.tz) doesn't parse")
            var utc = Int64(Self.start.timeIntervalSince1970)
            var mismatches = 0
            while utc < Int64(Self.end.timeIntervalSince1970) {
                let expected = zone.secondsFromGMT(for: Date(timeIntervalSince1970: TimeInterval(utc)))
                if info.local(utc: utc).seconds - utc != Int64(expected) {
                    mismatches += 1
                }
                utc += 15 * 60
            }
            XCTAssertEqual(mismatches, 0, "\(id): \(rule.tz)")
        }
    }

    /// Offsets and rules as the tz database has them. Names are whatever the
    /// platform calls the zone (EST, or <-05> where it says GMT-5); the radio
    /// never shows them.
    func testKnownRules() async throws {
        func info(_ id: String) throws -> (String, PosixTimeZone.Info) {
            let tz = TimeZoneRule.make(for: TimeZone(identifier: id)!, at: Self.start).tz
            return (tz, try XCTUnwrap(PosixTimeZone.parse(tz), tz))
        }
        var (tz, i) = try info("America/New_York")
        XCTAssertTrue(tz.hasSuffix(",M3.2.0,M11.1.0"), tz)
        XCTAssertEqual([i.stdOffsetS, i.dstOffsetS], [-18000, -14400])
        (tz, i) = try info("America/Phoenix")
        XCTAssertFalse(i.hasDst, tz)
        XCTAssertEqual(i.stdOffsetS, -25200)
        (tz, i) = try info("Pacific/Honolulu")
        XCTAssertEqual(i.stdOffsetS, -36000, tz)
        (tz, i) = try info("Europe/London")
        XCTAssertTrue(tz.hasSuffix(",M3.5.0/1,M10.5.0"), tz)
        (tz, i) = try info("Australia/Sydney")
        XCTAssertTrue(tz.hasSuffix(",M10.1.0,M4.1.0/3"), tz)
        (tz, i) = try info("Asia/Kolkata")
        XCTAssertTrue(tz.hasSuffix("-5:30"), tz)
        (tz, i) = try info("Australia/Lord_Howe")
        XCTAssertEqual(i.dstOffsetS - i.stdOffsetS, 1800, "a half-hour shift is written out: \(tz)")
    }

    func testNamesThatArentLettersAreQuoted() async {
        XCTAssertEqual(TimeZoneRule.name("GMT+10", offset: 36000), "<+10>")
        XCTAssertEqual(TimeZoneRule.name(nil, offset: -12600), "<-0330>")
        XCTAssertEqual(TimeZoneRule.name("AEST", offset: 36000), "AEST")
        XCTAssertEqual(TimeZoneRule.posixOffset(-18000), "5")
        XCTAssertEqual(TimeZoneRule.posixOffset(19800), "-5:30")
    }

    /// Morocco suspends daylight time for Ramadan, which no Mm.w.d rule can
    /// say: the radio gets the current offset and the app says so.
    func testInexpressibleZoneFallsBackToTheCurrentOffset() async throws {
        let zone = try XCTUnwrap(TimeZone(identifier: "Africa/Casablanca"))
        let rule = TimeZoneRule.make(for: zone, at: Self.start)
        XCTAssertFalse(rule.exact, rule.tz)
        let info = try XCTUnwrap(PosixTimeZone.parse(rule.tz))
        XCTAssertFalse(info.hasDst)
        XCTAssertEqual(info.stdOffsetS, zone.secondsFromGMT(for: Self.start))
    }

    func testEveryConnectionSyncsTheClock() async throws {
        let link = FakeRadioLink()
        var clock = Self.start
        let session = RadioSession(link: link, now: { clock }, timeZone: { TimeZone(identifier: "America/Chicago")! })
        session.connect()
        await session.settled()
        XCTAssertEqual(link.radio.utc, 1_767_225_600)
        XCTAssertTrue(link.radio.tz.hasSuffix("6CDT,M3.2.0,M11.1.0") || link.radio.tz.hasSuffix("6<-05>,M3.2.0,M11.1.0"),
                      link.radio.tz)
        XCTAssertEqual(session.timeRule?.exact, true)
        XCTAssertEqual(link.writes.first?.0, .time, "the clock is set before anything else")

        session.disconnect()
        clock = clock.addingTimeInterval(3600)
        link.window = .connect
        session.connect()
        await session.settled()
        XCTAssertEqual(link.radio.utc, 1_767_229_200)
    }
}

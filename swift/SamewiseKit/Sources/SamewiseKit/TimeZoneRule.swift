import Foundation
import GattModel

/// The phone's time zone as a POSIX TZ string for the radio's Time
/// characteristic: offsets from TimeZone, and the daylight-saving rules
/// from its next two transitions in Mm.w.d/time form. The string is checked
/// through the radio's own conversion (GattModel's port of tz.c) against
/// TimeZone for three years; a zone whose rules can't be written that way
/// gets its current offset only, which the app refreshes on every connection.
public struct TimeZoneRule: Equatable, Sendable {
    public var tz: String
    /// False when only the current offset could be expressed.
    public var exact: Bool

    /// How far ahead the rules are checked.
    static let checkedSeconds: TimeInterval = 3 * 366 * 86400

    public static func make(for zone: TimeZone, at now: Date) -> TimeZoneRule {
        if let tz = rules(zone, now), tz.utf8.count <= Codec.tzMax, matches(tz, zone, now) {
            return TimeZoneRule(tz: tz, exact: true)
        }
        let offset = zone.secondsFromGMT(for: now)
        let fixed = name(zone.abbreviation(for: now), offset: offset) + posixOffset(offset)
        return TimeZoneRule(tz: fixed, exact: !hasTransitions(zone, now))
    }

    // MARK: - Building the string

    static func hasTransitions(_ zone: TimeZone, _ now: Date) -> Bool {
        guard let t = zone.nextDaylightSavingTimeTransition(after: now) else { return false }
        return t.timeIntervalSince(now) < checkedSeconds
    }

    /// The rule string from the next two transitions, or nil if they aren't
    /// a daylight-saving pair.
    static func rules(_ zone: TimeZone, _ now: Date) -> String? {
        guard let t1 = zone.nextDaylightSavingTimeTransition(after: now) else {
            let offset = zone.secondsFromGMT(for: now)
            return name(zone.abbreviation(for: now), offset: offset) + posixOffset(offset)
        }
        guard let t2 = zone.nextDaylightSavingTimeTransition(after: t1) else { return nil }
        let (start, end) = zone.isDaylightSavingTime(for: t1) ? (t1, t2) : (t2, t1)
        guard zone.isDaylightSavingTime(for: start), !zone.isDaylightSavingTime(for: end) else { return nil }
        let stdOffset = zone.secondsFromGMT(for: end)
        let dstOffset = zone.secondsFromGMT(for: start)
        guard zone.secondsFromGMT(for: start - 1) == stdOffset,
              zone.secondsFromGMT(for: end - 1) == dstOffset else { return nil }

        var s = name(zone.abbreviation(for: end), offset: stdOffset) + posixOffset(stdOffset)
        s += name(zone.abbreviation(for: start), offset: dstOffset)
        if dstOffset != stdOffset + 3600 {
            s += posixOffset(dstOffset)
        }
        guard let startRule = rule(at: start, localOffset: stdOffset),
              let endRule = rule(at: end, localOffset: dstOffset) else { return nil }
        return s + "," + startRule + "," + endRule
    }

    /// Mm.w.d[/time] for a transition, in the wall time it happens at
    /// (standard time for the start, daylight time for the end).
    static func rule(at t: Date, localOffset: Int) -> String? {
        var cal = Calendar(identifier: .gregorian)
        cal.timeZone = TimeZone(secondsFromGMT: 0)!
        let wall = t.addingTimeInterval(TimeInterval(localOffset))
        let c = cal.dateComponents([.month, .day, .weekday, .hour, .minute, .second], from: wall)
        guard let month = c.month, let day = c.day, let weekday = c.weekday,
              let days = cal.range(of: .day, in: .month, for: wall)?.count else { return nil }
        // Week 5 means the last such weekday; a date in the month's last 7 days is written that way.
        let week = day + 7 > days ? 5 : (day - 1) / 7 + 1
        var r = "M\(month).\(week).\(weekday - 1)"
        let seconds = (c.hour ?? 0) * 3600 + (c.minute ?? 0) * 60 + (c.second ?? 0)
        if seconds != 2 * 3600 {
            r += "/" + hms(seconds)
        }
        return r
    }

    /// The zone's abbreviation if POSIX allows it bare (3 to 15 letters),
    /// otherwise a quoted name from the offset, such as <+0530>.
    static func name(_ abbreviation: String?, offset: Int) -> String {
        if let a = abbreviation, (3...15).contains(a.count),
           a.unicodeScalars.allSatisfy({ ("A"..."Z").contains($0) || ("a"..."z").contains($0) }) {
            return a
        }
        let sign = offset < 0 ? "-" : "+"
        let h = abs(offset) / 3600
        let m = abs(offset) % 3600 / 60
        return "<" + sign + String(format: "%02d", h) + (m == 0 ? "" : String(format: "%02d", m)) + ">"
    }

    /// POSIX offsets count west of Greenwich: UTC-5 is "5".
    static func posixOffset(_ secondsEast: Int) -> String {
        let west = -secondsEast
        return (west < 0 ? "-" : "") + hms(abs(west))
    }

    static func hms(_ s: Int) -> String {
        let h = s / 3600
        let m = s % 3600 / 60
        let sec = s % 60
        if sec != 0 {
            return String(format: "%d:%02d:%02d", h, m, sec)
        }
        return m != 0 ? String(format: "%d:%02d", h, m) : "\(h)"
    }

    // MARK: - Checking it

    /// The radio's conversion agrees with TimeZone at, and just before,
    /// every transition in the checked period, and midway between them.
    static func matches(_ tz: String, _ zone: TimeZone, _ now: Date) -> Bool {
        guard let info = PosixTimeZone.parse(tz) else { return false }
        func agrees(_ d: Date) -> Bool {
            let utc = Int64(d.timeIntervalSince1970.rounded(.down))
            return info.local(utc: utc).seconds - utc == Int64(zone.secondsFromGMT(for: d))
        }
        let limit = now.addingTimeInterval(checkedSeconds)
        var t = now
        while true {
            guard agrees(t) else { return false }
            guard let next = zone.nextDaylightSavingTimeTransition(after: t), next < limit else {
                return agrees(limit)
            }
            guard agrees(t.addingTimeInterval(next.timeIntervalSince(t) / 2)), agrees(next - 1), agrees(next)
            else { return false }
            t = next
        }
    }
}

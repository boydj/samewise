import Foundation
import GattModel

/// The home screen's lines, from the radio's Status characteristic.
public struct StatusSummary: Equatable, Sendable {
    public struct Warning: Equatable, Sendable {
        public var name: String
        public var text: String
    }

    public var battery: String
    public var hoursLeft: String
    public var signal: String
    public var channel: String
    public var lastWeeklyTest: String
    public var warnings: [Warning]
    public var locked: Bool

    public init(_ s: Codec.Status, document: GattDocument = .bundled, now: Date, zone: TimeZone = .current) {
        battery = "\(s.batteryPercent)%"
        hoursLeft = s.hoursLeft == 0xFFFF ? "Not known yet" : Self.duration(hours: Int(s.hoursLeft))
        signal = "SNR \(s.snrDb) dB, RSSI \(s.rssiDbuv) dBµV"
        channel = WeatherChannel.title(s.channel)
        lastWeeklyTest = s.lastRwtUtc < 0
            ? "None received yet"
            : LogRow.relative(Date(timeIntervalSince1970: TimeInterval(s.lastRwtUtc)), now: now, zone: zone)
        warnings = document.healthFlags.filter { s.healthFlags & $0.value != 0 }.map {
            Warning(name: $0.name, text: Self.warningText($0.name))
        }
        locked = s.locked != 0
    }

    static func duration(hours: Int) -> String {
        hours < 48 ? "\(hours) hours" : "\(hours / 24) days"
    }

    /// The radio's health warnings (spec: Health monitoring), by their names in docs/gatt.json.
    public static func warningText(_ name: String) -> String {
        switch name {
        case "NO_SIGNAL": return "No signal: the weather channel has been too weak for 10 minutes."
        case "NO_WEEKLY_TEST": return "No weekly test received in 8 days. Check the antenna and channel."
        case "BATTERY_LOW": return "Battery low. Charge the radio."
        case "BATTERY_CRITICAL": return "Battery critical. Charge the radio now."
        case "TUNER_FAULT": return "The radio's tuner reported a fault."
        default: return "The radio reported a problem (\(name))."
        }
    }
}

/// A SAME header, split into its fields: ZCZC-ORG-EEE-PSSCCC...+TTTT-JJJHHMM-LLLLLLLL-
public struct SameHeader: Equatable, Sendable {
    public var originator: String
    public var event: String
    public var locations: [String]
    /// Purge time, minutes.
    public var purgeMinutes: Int
    public var issueDay: Int
    public var issueHour: Int
    public var issueMinute: Int
    public var station: String

    public init?(_ raw: String) {
        let parts = raw.split(separator: "-", omittingEmptySubsequences: false).map(String.init)
        guard parts.count >= 7, parts[0] == "ZCZC" else { return nil }
        // Locations run until the one carrying "+TTTT".
        guard let plusIndex = parts.firstIndex(where: { $0.contains("+") }), plusIndex >= 3,
              parts.count > plusIndex + 2 else { return nil }
        let lastLoc = parts[plusIndex].split(separator: "+").map(String.init)
        guard lastLoc.count == 2, lastLoc[1].count == 4, let purge = Int(lastLoc[1]) else { return nil }
        let issue = parts[plusIndex + 1]
        guard issue.count == 7, let d = Int(issue.prefix(3)), let h = Int(issue.dropFirst(3).prefix(2)),
              let m = Int(issue.suffix(2)) else { return nil }
        originator = parts[1]
        event = parts[2]
        locations = Array(parts[3..<plusIndex]) + [lastLoc[0]]
        purgeMinutes = purge / 100 * 60 + purge % 100
        issueDay = d
        issueHour = h
        issueMinute = m
        station = parts[plusIndex + 2]
    }
}

/// One alert log entry as the log screen shows it.
public struct LogRow: Equatable, Sendable {
    public var title: String
    public var outcome: String
    public var received: String
    public var locations: [String]
    public var raw: String

    public init(_ e: Codec.LogEntry, events: [Codec.Event], document: GattDocument = .bundled, now: Date,
                zone: TimeZone = .current) {
        raw = e.raw
        let header = SameHeader(e.raw)
        if let header {
            title = events.first { $0.code == header.event }?.name ?? "Unknown event (\(header.event))"
            locations = header.locations
        } else {
            title = e.raw == "TEST ALERT" ? "Test alert" : e.raw
            locations = []
        }
        let name = document.logOutcomes.first { $0.value == UInt32(e.outcome) }?.name ?? ""
        outcome = Self.outcomeText(name)
        received = e.receivedUtc < 0
            ? "Before the clock was set"
            : Self.relative(Date(timeIntervalSince1970: TimeInterval(e.receivedUtc)), now: now, zone: zone)
    }

    /// By their names in docs/gatt.json.
    public static func outcomeText(_ name: String) -> String {
        switch name {
        case "ALERTED": return "Alerted"
        case "FILTERED": return "Logged only: your filter doesn't alert for this event"
        case "UNKNOWN": return "Logged only: the radio's event table doesn't know this code"
        case "EXPIRED": return "Logged only: already expired when received"
        case "TEST": return "Test alert from this app"
        default: return "Logged"
        }
    }

    /// "Today 14:05", "Yesterday 09:30", or a date, in the phone's time zone.
    public static func relative(_ d: Date, now: Date, zone: TimeZone) -> String {
        var cal = Calendar(identifier: .gregorian)
        cal.timeZone = zone
        let time = DateFormatter()
        time.calendar = cal
        time.timeZone = zone
        time.locale = Locale(identifier: "en_US_POSIX")
        time.dateFormat = "HH:mm"
        if cal.isDate(d, inSameDayAs: now) {
            return "Today " + time.string(from: d)
        }
        if let y = cal.date(byAdding: .day, value: -1, to: now), cal.isDate(d, inSameDayAs: y) {
            return "Yesterday " + time.string(from: d)
        }
        time.dateFormat = "d MMM yyyy HH:mm"
        return time.string(from: d)
    }
}

import Foundation

/// ATT errors the radio returns (docs/gatt.json "errors", plus two standard ones).
public enum AttError: Error, Equatable, CaseIterable, Sendable {
    case length, value, schema, storage, sequence, busy, index
    case readNotPermitted, writeNotPermitted

    public var code: UInt8 {
        switch self {
        case .length: return 0x0D
        case .value: return 0x13
        case .schema: return 0x80
        case .storage: return 0x81
        case .sequence: return 0x82
        case .busy: return 0x83
        case .index: return 0x84
        case .readNotPermitted: return 0x02
        case .writeNotPermitted: return 0x03
        }
    }

    /// The name in docs/gatt.json, for the radio's own errors.
    public var gattName: String? {
        switch self {
        case .length: return "LENGTH"
        case .value: return "VALUE"
        case .schema: return "SCHEMA"
        case .storage: return "STORAGE"
        case .sequence: return "SEQUENCE"
        case .busy: return "BUSY"
        case .index: return "INDEX"
        case .readNotPermitted, .writeNotPermitted: return nil
        }
    }
}

/// The value codec, a port of the firmware's services/ble/codec.c: same
/// layouts, same checks, same errors.
public enum Codec {
    public static let schema: UInt8 = 1
    public static let maxCounties = 16
    public static let maxPresets = 8
    public static let tzMax = 48
    public static let channelMax: UInt8 = 7
    public static let filterBytes = 16
    public static let filterPresets: UInt8 = 4
    public static let eventTableMax = 128
    public static let eventNameMax = 31
    public static let eventClasses: UInt8 = 5
    public static let statusLength = 20

    public enum Band: UInt8, Sendable {
        case fm = 0, am = 1, wb = 2
    }

    public struct Preset: Equatable, Sendable {
        public var band: UInt8
        public var khz: UInt32

        public init(band: UInt8, khz: UInt32) {
            self.band = band
            self.khz = khz
        }
    }

    public struct Event: Equatable, Sendable {
        public var code: String
        public var eventClass: UInt8
        public var name: String

        public init(code: String, eventClass: UInt8, name: String) {
            self.code = code
            self.eventClass = eventClass
            self.name = name
        }
    }

    public enum EventTableWrite: Equatable, Sendable {
        case select(index: UInt8)
        case begin(version: UInt16, count: UInt8)
        case entry(index: UInt8, event: Event)
        case commit
        case abort
    }

    public enum Command: UInt8, Sendable {
        case testAlert = 1, clearLog = 2, factoryReset = 3
    }

    public enum ControlResult: UInt8, Sendable {
        case done = 0, awaitingConfirmation = 1, cancelled = 2, rejected = 3
    }

    public struct Status: Equatable, Sendable {
        public var batteryPercent: UInt8 = 100
        public var hoursLeft: UInt16 = 0xFFFF
        public var snrDb: Int8 = 25
        public var rssiDbuv: Int8 = 40
        public var channel: UInt8 = 7
        public var lastRwtUtc: Int64 = -1
        public var healthFlags: UInt32 = 0
        public var locked: UInt8 = 0

        public init() {}
    }

    public struct LogEntry: Equatable, Sendable {
        public var receivedUtc: Int64
        public var outcome: UInt8
        public var flags: UInt8
        public var raw: String

        public init(receivedUtc: Int64, outcome: UInt8, flags: UInt8, raw: String) {
            self.receivedUtc = receivedUtc
            self.outcome = outcome
            self.flags = flags
            self.raw = raw
        }
    }

    // MARK: - Helpers

    static func head(_ b: [UInt8], min: Int) throws {
        guard !b.isEmpty else { throw AttError.length }
        guard b[0] == schema else { throw AttError.schema }
        guard b.count >= min else { throw AttError.length }
    }

    static func printable(_ b: ArraySlice<UInt8>) -> Bool {
        b.allSatisfy { $0 >= 0x20 && $0 <= 0x7E }
    }

    static func le16(_ v: UInt16) -> [UInt8] { [UInt8(v & 0xFF), UInt8(v >> 8)] }

    static func le32(_ v: UInt32) -> [UInt8] { le16(UInt16(v & 0xFFFF)) + le16(UInt16(v >> 16)) }

    static func le64(_ v: Int64) -> [UInt8] {
        let u = UInt64(bitPattern: v)
        return le32(UInt32(u & 0xFFFF_FFFF)) + le32(UInt32(u >> 32))
    }

    static func get16(_ b: [UInt8], _ i: Int) -> UInt16 { UInt16(b[i]) | UInt16(b[i + 1]) << 8 }

    static func get32(_ b: [UInt8], _ i: Int) -> UInt32 {
        UInt32(get16(b, i)) | UInt32(get16(b, i + 2)) << 16
    }

    static func get64(_ b: [UInt8], _ i: Int) -> Int64 {
        Int64(bitPattern: UInt64(get32(b, i)) | UInt64(get32(b, i + 4)) << 32)
    }

    // MARK: - Counties: u8 schema, u8 count, count x "PSSCCC"

    public static func decodeCounties(_ b: [UInt8]) throws -> [String] {
        try head(b, min: 2)
        guard Int(b[1]) <= maxCounties else { throw AttError.value }
        guard b.count == 2 + 6 * Int(b[1]) else { throw AttError.length }
        var codes: [String] = []
        for i in 0..<Int(b[1]) {
            let digits = b[(2 + 6 * i)..<(8 + 6 * i)]
            guard digits.allSatisfy({ $0 >= 0x30 && $0 <= 0x39 }) else { throw AttError.value }
            codes.append(String(decoding: digits, as: UTF8.self))
        }
        return codes
    }

    public static func encodeCounties(_ codes: [String]) -> [UInt8] {
        [schema, UInt8(codes.count)] + codes.flatMap { Array($0.utf8) }
    }

    // MARK: - Mode: u8 schema, u8 mode, u8 channel

    public static func decodeMode(_ b: [UInt8]) throws -> (mode: UInt8, channel: UInt8) {
        try head(b, min: 3)
        guard b.count == 3 else { throw AttError.length }
        guard b[1] <= 1, b[2] <= channelMax else { throw AttError.value }
        return (b[1], b[2])
    }

    public static func encodeMode(mode: UInt8, channel: UInt8) -> [UInt8] { [schema, mode, channel] }

    // MARK: - Event filter: u8 schema, u8 preset, 16-byte bitmap

    public static func decodeFilter(_ b: [UInt8]) throws -> (preset: UInt8, bitmap: [UInt8]) {
        try head(b, min: 2 + filterBytes)
        guard b.count == 2 + filterBytes else { throw AttError.length }
        guard b[1] < filterPresets else { throw AttError.value }
        return (b[1], Array(b[2...]))
    }

    public static func encodeFilter(preset: UInt8, bitmap: [UInt8]) -> [UInt8] { [schema, preset] + bitmap }

    // MARK: - Time: u8 schema, i64 UTC, u8 tz_len, POSIX TZ

    public static func decodeTime(_ b: [UInt8]) throws -> (utc: Int64, tz: String) {
        try head(b, min: 10)
        let tzLen = Int(b[9])
        guard tzLen >= 1, tzLen <= tzMax else { throw AttError.value }
        guard b.count == 10 + tzLen else { throw AttError.length }
        let utc = get64(b, 1)
        guard utc >= 0, printable(b[10...]) else { throw AttError.value }
        let tz = String(decoding: b[10...], as: UTF8.self)
        guard PosixTimeZone.isValid(tz) else { throw AttError.value }
        return (utc, tz)
    }

    public static func encodeTime(utc: Int64, tz: String) -> [UInt8] {
        [schema] + le64(utc) + [UInt8(tz.utf8.count)] + Array(tz.utf8)
    }

    // MARK: - Presets: u8 schema, u8 count, count x (u8 band, u32 kHz)

    public static func presetValid(_ p: Preset) -> Bool {
        switch Band(rawValue: p.band) {
        case .fm: return p.khz >= 87500 && p.khz <= 108000
        case .am: return p.khz >= 520 && p.khz <= 1710
        case .wb: return p.khz >= 162400 && p.khz <= 162550 && (p.khz - 162400) % 25 == 0
        case nil: return false
        }
    }

    public static func decodePresets(_ b: [UInt8]) throws -> [Preset] {
        try head(b, min: 2)
        guard Int(b[1]) <= maxPresets else { throw AttError.value }
        guard b.count == 2 + 5 * Int(b[1]) else { throw AttError.length }
        var presets: [Preset] = []
        for i in 0..<Int(b[1]) {
            let p = Preset(band: b[2 + 5 * i], khz: get32(b, 3 + 5 * i))
            guard presetValid(p) else { throw AttError.value }
            presets.append(p)
        }
        return presets
    }

    public static func encodePresets(_ presets: [Preset]) -> [UInt8] {
        [schema, UInt8(presets.count)] + presets.flatMap { [$0.band] + le32($0.khz) }
    }

    // MARK: - Status (read, notify)

    public static func encodeStatus(_ s: Status) -> [UInt8] {
        var b: [UInt8] = [schema, s.batteryPercent]
        b += le16(s.hoursLeft)
        b += [UInt8(bitPattern: s.snrDb), UInt8(bitPattern: s.rssiDbuv), s.channel]
        b += le64(s.lastRwtUtc)
        b += le32(s.healthFlags)
        b.append(s.locked)
        return b
    }

    public static func decodeStatus(_ b: [UInt8]) throws -> Status {
        try head(b, min: statusLength)
        guard b.count == statusLength else { throw AttError.length }
        guard b[1] <= 100, b[6] <= channelMax, b[19] <= 1 else { throw AttError.value }
        var s = Status()
        s.batteryPercent = b[1]
        s.hoursLeft = get16(b, 2)
        s.snrDb = Int8(bitPattern: b[4])
        s.rssiDbuv = Int8(bitPattern: b[5])
        s.channel = b[6]
        s.lastRwtUtc = get64(b, 7)
        s.healthFlags = get32(b, 15)
        s.locked = b[19]
        return s
    }

    // MARK: - Event table

    static func encodeEntry(_ e: Event) -> [UInt8] {
        Array(e.code.utf8) + [e.eventClass, UInt8(e.name.utf8.count)] + Array(e.name.utf8)
    }

    static func decodeEntry(_ b: ArraySlice<UInt8>) throws -> Event {
        let p = Array(b)
        guard p.count >= 5 else { throw AttError.length }
        let nameLen = Int(p[4])
        guard nameLen >= 1, nameLen <= eventNameMax else { throw AttError.value }
        guard p.count == 5 + nameLen else { throw AttError.length }
        guard p[0..<3].allSatisfy({ $0 >= 0x41 && $0 <= 0x5A }), p[3] < eventClasses,
              printable(p[5...]) else { throw AttError.value }
        return Event(code: String(decoding: p[0..<3], as: UTF8.self), eventClass: p[3],
                     name: String(decoding: p[5...], as: UTF8.self))
    }

    public static func decodeEventTableWrite(_ b: [UInt8]) throws -> EventTableWrite {
        try head(b, min: 2)
        switch b[1] {
        case 0:
            guard b.count == 3 else { throw AttError.length }
            return .select(index: b[2])
        case 1:
            guard b.count == 5 else { throw AttError.length }
            guard Int(b[4]) <= eventTableMax else { throw AttError.value }
            return .begin(version: get16(b, 2), count: b[4])
        case 2:
            guard b.count >= 3 else { throw AttError.length }
            let event = try decodeEntry(b[3...])
            return .entry(index: b[2], event: event)
        case 3, 4:
            guard b.count == 2 else { throw AttError.length }
            return b[1] == 3 ? .commit : .abort
        default:
            throw AttError.value
        }
    }

    public static func encodeEventTableWrite(_ w: EventTableWrite) -> [UInt8] {
        switch w {
        case .select(let index): return [schema, 0, index]
        case .begin(let version, let count): return [schema, 1] + le16(version) + [count]
        case .entry(let index, let event): return [schema, 2, index] + encodeEntry(event)
        case .commit: return [schema, 3]
        case .abort: return [schema, 4]
        }
    }

    public static func encodeEventTableRead(version: UInt16, count: UInt8, index: UInt8,
                                            event: Event) -> [UInt8] {
        [schema] + le16(version) + [count, index] + encodeEntry(event)
    }

    /// An event table read: one entry, with the table's version and size.
    public struct EventTableRead: Equatable, Sendable {
        public var version: UInt16
        public var count: UInt8
        public var index: UInt8
        public var event: Event

        public init(version: UInt16, count: UInt8, index: UInt8, event: Event) {
            self.version = version
            self.count = count
            self.index = index
            self.event = event
        }
    }

    public static func decodeEventTableRead(_ b: [UInt8]) throws -> EventTableRead {
        try head(b, min: 5)
        let event = try decodeEntry(b[5...])
        guard b[4] < b[3] else { throw AttError.value }
        return EventTableRead(version: get16(b, 1), count: b[3], index: b[4], event: event)
    }

    // MARK: - Alert log

    /// An alert log read or notification: one entry, with the log's size.
    public struct LogRead: Equatable, Sendable {
        public var count: UInt8
        public var index: UInt8
        public var entry: LogEntry

        public init(count: UInt8, index: UInt8, entry: LogEntry) {
            self.count = count
            self.index = index
            self.entry = entry
        }
    }

    public static func decodeLogEntry(_ b: [UInt8]) throws -> LogRead {
        try head(b, min: 14)
        let len = Int(b[13])
        guard b.count == 14 + len else { throw AttError.length }
        guard b[2] < b[1], printable(b[14...]) else { throw AttError.value }
        let entry = LogEntry(receivedUtc: get64(b, 3), outcome: b[11], flags: b[12],
                             raw: String(decoding: b[14...], as: UTF8.self))
        return LogRead(count: b[1], index: b[2], entry: entry)
    }

    public static func encodeLogSelect(_ index: UInt8) -> [UInt8] { [schema, index] }

    public static func decodeLogSelect(_ b: [UInt8]) throws -> UInt8 {
        try head(b, min: 2)
        guard b.count == 2 else { throw AttError.length }
        return b[1]
    }

    public static func encodeLogEntry(count: UInt8, index: UInt8, _ e: LogEntry) -> [UInt8] {
        [schema, count, index] + le64(e.receivedUtc) + [e.outcome, e.flags, UInt8(e.raw.utf8.count)]
            + Array(e.raw.utf8)
    }

    // MARK: - Control

    public static func decodeCommand(_ b: [UInt8]) throws -> Command {
        try head(b, min: 2)
        guard b.count == 2 else { throw AttError.length }
        guard let c = Command(rawValue: b[1]) else { throw AttError.value }
        return c
    }

    public static func encodeCommand(_ c: Command) -> [UInt8] { [schema, c.rawValue] }

    public static func decodeControlIndication(_ b: [UInt8]) throws -> (Command, ControlResult) {
        try head(b, min: 3)
        guard b.count == 3 else { throw AttError.length }
        guard let c = Command(rawValue: b[1]), let r = ControlResult(rawValue: b[2]) else { throw AttError.value }
        return (c, r)
    }

    public static func encodeControlIndication(_ c: Command, _ r: ControlResult) -> [UInt8] {
        [schema, c.rawValue, r.rawValue]
    }
}
